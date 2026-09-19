#include "process.h"

#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <vector>

#include "util.h"

namespace proc {
namespace {

constexpr size_t kPathBufferChars = 32768;

// 读取某个进程的完整镜像路径。
bool QueryProcessImagePath(DWORD pid, std::wstring& out) {
    out.clear();
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    const BOOL ok = ::QueryFullProcessImageNameW(process, 0, buffer.data(), &size);
    ::CloseHandle(process);
    if (ok == FALSE || size == 0) {
        return false;
    }
    out.assign(buffer.data(), size);
    return true;
}

// 解析 .lnk。失败返回空串。
std::wstring ResolveShortcut(const std::wstring& shortcutPath) {
    IShellLinkW* link = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&link))) ||
        link == nullptr) {
        return std::wstring();
    }

    std::wstring result;
    IPersistFile* persist = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persist))) &&
        persist != nullptr) {
        if (SUCCEEDED(persist->Load(shortcutPath.c_str(), STGM_READ))) {
            std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
            WIN32_FIND_DATAW findData;
            ::ZeroMemory(&findData, sizeof(findData));
            if (SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), &findData,
                                        SLGP_UNCPRIORITY)) &&
                buffer[0] != L'\0') {
                result.assign(buffer.data());
            }
        }
        persist->Release();
    }
    link->Release();
    return result;
}

struct WindowSearchContext {
    DWORD pid;
    int posted;
};

BOOL CALLBACK PostCloseToWindow(HWND window, LPARAM param) {
    WindowSearchContext* context = reinterpret_cast<WindowSearchContext*>(param);
    DWORD owner = 0;
    ::GetWindowThreadProcessId(window, &owner);
    if (owner == context->pid) {
        if (::PostMessageW(window, WM_CLOSE, 0, 0) != FALSE) {
            ++context->posted;
        }
    }
    return TRUE;
}

}  // namespace

std::wstring ResolveExecutable(const std::wstring& path) {
    const std::wstring full = util::NormalizePath(path);
    if (!util::HasExtension(full, L".lnk")) {
        return full;
    }
    const std::wstring target = ResolveShortcut(full);
    if (!target.empty() && util::FileExists(target)) {
        return util::NormalizePath(target);
    }
    // 解析不出来就保留快捷方式本身：ShellExecute 自己也能处理 .lnk。
    return full;
}

bool Launch(const std::wstring& path, DWORD* outPid, std::wstring* outError) {
    if (outPid != nullptr) {
        *outPid = 0;
    }

    SHELLEXECUTEINFOW info;
    ::ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    // SEE_MASK_FLAG_NO_UI：不要系统弹窗，出错我们自己提示。
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (::ShellExecuteExW(&info) == FALSE) {
        const DWORD code = ::GetLastError();
        if (outError != nullptr) {
            if (code == ERROR_CANCELLED) {
                *outError = L"启动被取消。";
            } else {
                *outError = L"启动失败：" + util::FormatWinError(code);
            }
        }
        return false;
    }

    if (info.hProcess != nullptr) {
        if (outPid != nullptr) {
            *outPid = ::GetProcessId(info.hProcess);
        }
        ::CloseHandle(info.hProcess);
    }
    return true;
}

bool IsProcessAlive(DWORD pid) {
    if (pid == 0) {
        return false;
    }
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return false;  // 打不开通常意味着进程已经退出
    }
    const DWORD wait = ::WaitForSingleObject(process, 0);
    ::CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}

std::vector<DWORD> FindRunning(const std::wstring& exePath, bool* matchedByPath) {
    std::vector<DWORD> exactMatches;
    std::vector<DWORD> nameMatches;
    if (matchedByPath != nullptr) {
        *matchedByPath = false;
    }

    const std::wstring wantedPath = util::NormalizePath(exePath);
    const std::wstring wantedName = util::FileNameOf(wantedPath);
    if (wantedName.empty()) {
        return exactMatches;
    }

    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return exactMatches;
    }

    const DWORD self = ::GetCurrentProcessId();
    PROCESSENTRY32W entry;
    ::ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    if (::Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            const DWORD pid = entry.th32ProcessID;
            // 跳过 Idle、System 和自己。
            if (pid == 0 || pid == 4 || pid == self) {
                continue;
            }

            std::wstring imagePath;
            const bool havePath = QueryProcessImagePath(pid, imagePath);

            if (havePath && util::EqualsIgnoreCase(util::NormalizePath(imagePath), wantedPath)) {
                exactMatches.push_back(pid);
                continue;
            }

            // 兜底：有些程序会从更新目录里重新拉起自己，完整路径对不上，
            // 这时按可执行文件名匹配，但排除系统目录，避免误伤同名系统进程。
            if (util::EqualsIgnoreCase(entry.szExeFile, wantedName) &&
                !util::IsUnderWindowsDirectory(imagePath)) {
                nameMatches.push_back(pid);
            }
        } while (::Process32NextW(snapshot, &entry) != FALSE);
    }
    ::CloseHandle(snapshot);

    if (!exactMatches.empty()) {
        if (matchedByPath != nullptr) {
            *matchedByPath = true;
        }
        return exactMatches;
    }
    return nameMatches;
}

bool IsRunning(const std::wstring& exePath) {
    return !FindRunning(exePath, nullptr).empty();
}

std::vector<DWORD> RequestClose(const std::vector<DWORD>& pids, int waitMs) {
    std::vector<DWORD> alive;
    if (pids.empty()) {
        return alive;
    }

    // 第一步：请目标自己退出，这样它能正常保存数据、清理托盘图标。
    for (const DWORD pid : pids) {
        WindowSearchContext context;
        context.pid = pid;
        context.posted = 0;
        ::EnumWindows(PostCloseToWindow, reinterpret_cast<LPARAM>(&context));
        util::DebugLog(L"向 PID " + std::to_wstring(static_cast<unsigned long>(pid)) + L" 的 " +
                       std::to_wstring(context.posted) + L" 个窗口发送了 WM_CLOSE");
    }

    // 第二步：轮询等待。用轮询而不是 WaitForMultipleObjects，避免后者 64 个句柄的上限。
    const ULONGLONG deadline = ::GetTickCount64() + static_cast<ULONGLONG>(std::max(0, waitMs));
    for (;;) {
        alive.clear();
        for (const DWORD pid : pids) {
            if (IsProcessAlive(pid)) {
                alive.push_back(pid);
            }
        }
        if (alive.empty() || ::GetTickCount64() >= deadline) {
            return alive;
        }
        ::Sleep(60);
    }
}

int ForceClose(const std::vector<DWORD>& pids) {
    int closed = 0;
    for (const DWORD pid : pids) {
        HANDLE process = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (process == nullptr) {
            continue;
        }
        if (::TerminateProcess(process, 0) != FALSE) {
            ::WaitForSingleObject(process, 1000);
            ++closed;
        }
        ::CloseHandle(process);
    }
    if (closed > 0) {
        util::DebugLog(L"强制结束了 " + std::to_wstring(closed) + L" 个进程");
    }
    return closed;
}

}  // namespace proc

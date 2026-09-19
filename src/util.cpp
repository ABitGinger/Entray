#include "util.h"

#include <shlobj.h>
#include <wchar.h>

#include <algorithm>
#include <string>

namespace util {
namespace {

// 项目里所有路径缓冲区都用这个长度：足够放下长路径，且不依赖 MAX_PATH。
constexpr size_t kPathBufferChars = 32768;

const wchar_t* const kWhitespace = L" \t\r\n";

}  // namespace

bool EqualsIgnoreCase(const std::wstring& lhs, const std::wstring& rhs) {
    return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
}

std::wstring Trim(const std::wstring& text) {
    const size_t begin = text.find_first_not_of(kWhitespace);
    if (begin == std::wstring::npos) {
        return std::wstring();
    }
    const size_t end = text.find_last_not_of(kWhitespace);
    return text.substr(begin, end - begin + 1);
}

std::wstring TrimQuotes(const std::wstring& text) {
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"') {
        return text.substr(1, text.size() - 2);
    }
    return text;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return path;
    }
    return path.substr(slash + 1);
}

std::wstring FileStemOf(const std::wstring& path) {
    std::wstring name = FileNameOf(path);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        name.erase(dot);
    }
    return name;
}

std::wstring DirectoryOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return std::wstring();
    }
    if (slash == 2 && path.size() > 2 && path[1] == L':') {
        return path.substr(0, 3);  // "C:\"
    }
    return path.substr(0, slash);
}

bool HasExtension(const std::wstring& path, const wchar_t* extension) {
    const std::wstring actual = FileNameOf(path);
    const std::wstring wanted(extension);
    if (actual.size() < wanted.size()) {
        return false;
    }
    return EqualsIgnoreCase(actual.substr(actual.size() - wanted.size()), wanted);
}

std::wstring Elide(const std::wstring& text, size_t maxChars) {
    if (text.size() <= maxChars || maxChars < 2) {
        return text;
    }
    return text.substr(0, maxChars - 1) + L"\u2026";
}

std::wstring ExpandEnvironment(const std::wstring& path) {
    if (path.empty()) {
        return std::wstring();
    }
    std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
    DWORD length = ::ExpandEnvironmentStringsW(path.c_str(), buffer.data(),
                                               static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return path;
    }
    if (length > buffer.size()) {
        buffer.assign(length, L'\0');
        length = ::ExpandEnvironmentStringsW(path.c_str(), buffer.data(),
                                             static_cast<DWORD>(buffer.size()));
        if (length == 0 || length > buffer.size()) {
            return path;
        }
    }
    // 返回值包含结尾的 NUL。
    return std::wstring(buffer.data(), length - 1);
}

std::wstring NormalizePath(const std::wstring& path) {
    std::wstring working = ExpandEnvironment(TrimQuotes(Trim(path)));
    if (working.empty()) {
        return std::wstring();
    }

    // 去掉 Win32 长路径前缀，保证两侧比较时格式一致。
    if (working.size() >= 4 && working.compare(0, 4, L"\\\\?\\") == 0) {
        working.erase(0, 4);
        if (working.size() >= 4 && _wcsnicmp(working.c_str(), L"UNC\\", 4) == 0) {
            working = L"\\\\" + working.substr(4);
        }
    } else if (working.size() >= 4 && working.compare(0, 4, L"\\\\.\\") == 0) {
        working.erase(0, 4);
    }

    std::vector<wchar_t> buffer(kPathBufferChars, L'\0');
    const DWORD length = ::GetFullPathNameW(working.c_str(), static_cast<DWORD>(buffer.size()),
                                           buffer.data(), nullptr);
    if (length > 0 && length < buffer.size()) {
        working.assign(buffer.data(), length);
    }

    // 去掉结尾的反斜杠，但保留 "C:\" 这种根目录。
    while (working.size() > 3 && (working.back() == L'\\' || working.back() == L'/')) {
        working.pop_back();
    }
    return working;
}

std::wstring GetModulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = ::GetModuleFileNameW(module, buffer.data(),
                                                  static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return std::wstring();
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool IsUnderWindowsDirectory(const std::wstring& fullPath) {
    if (fullPath.empty()) {
        return false;
    }
    wchar_t windowsDirectory[MAX_PATH] = {};
    const UINT length = ::GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    std::wstring prefix(windowsDirectory, length);
    if (prefix.empty()) {
        return false;
    }
    if (prefix.back() != L'\\') {
        prefix.push_back(L'\\');
    }
    if (fullPath.size() < prefix.size()) {
        return false;
    }
    return EqualsIgnoreCase(fullPath.substr(0, prefix.size()), prefix);
}

std::wstring DesktopPath() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &raw)) && raw != nullptr) {
        std::wstring result(raw);
        ::CoTaskMemFree(raw);
        return result;
    }
    return std::wstring();
}

bool FileExists(const std::wstring& path) {
    if (path.empty()) {
        return false;
    }
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring FormatWinError(DWORD code) {
    if (code == 0) {
        return L"未知错误";
    }
    wchar_t buffer[512] = {};
    const DWORD length = ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                          nullptr, code,
                                          MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer,
                                          static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
                                          nullptr);
    if (length == 0) {
        return L"错误码 " + std::to_wstring(static_cast<unsigned long>(code));
    }
    std::wstring message(buffer, length);
    while (!message.empty()) {
        const wchar_t tail = message.back();
        if (tail == L'\r' || tail == L'\n' || tail == L' ' || tail == L'.') {
            message.pop_back();
            continue;
        }
        break;
    }
    if (message.empty()) {
        return L"错误码 " + std::to_wstring(static_cast<unsigned long>(code));
    }
    return message;
}

void DebugLog(const std::wstring& message) {
    // 没有调试器接手时这一步几乎没有开销；需要排查问题时用 DebugView 就能看到。
    ::OutputDebugStringW((L"[Entray] " + message + L"\n").c_str());
}

}  // namespace util

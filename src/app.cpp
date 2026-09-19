#include "app.h"

// WIN32_LEAN_AND_MEAN 会把 commdlg.h 挡在 windows.h 外面，OPENFILENAME 得自己引。
#include <commdlg.h>

#include <algorithm>
#include <string>
#include <vector>

#include "common.h"
#include "config.h"
#include "process.h"
#include "resource.h"
#include "util.h"
#include "version.h"

namespace entray {
namespace {

// 菜单项里显示的程序名最多保留多少个字符。
constexpr size_t kMenuNameLimit = 32;

// 加载程序自带的图标。图标已经编译进 exe 资源，不依赖任何外部文件。
HICON LoadAppIcon(HINSTANCE instance, int width, int height) {
    return reinterpret_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(IDI_ENTRAY), IMAGE_ICON,
                                                width, height, LR_DEFAULTCOLOR));
}

// 取目标程序的图标。优先用 Shell 提供的（对 .lnk / .exe / .bat 都合适），
// 失败时退回 ExtractIconEx。返回的句柄归调用方所有，需要 DestroyIcon。
HICON LoadTargetIcon(const std::wstring& path) {
    if (path.empty()) {
        return nullptr;
    }

    SHFILEINFOW info;
    ::ZeroMemory(&info, sizeof(info));
    if (::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON) != 0 &&
        info.hIcon != nullptr) {
        return info.hIcon;
    }

    HICON large = nullptr;
    HICON small = nullptr;
    const UINT extracted = ::ExtractIconExW(path.c_str(), 0, &large, &small, 1);
    if (large != nullptr) {
        ::DestroyIcon(large);
    }
    if (extracted > 0 && small != nullptr) {
        return small;
    }
    return nullptr;
}

std::wstring MakeMenuLabel(const wchar_t* verb, const std::wstring& name) {
    if (name.empty()) {
        return std::wstring(verb) + L"（尚未选择程序）";
    }
    return std::wstring(verb) + L" " + name;
}

int TipBufferSize() {
    return static_cast<int>(sizeof(NOTIFYICONDATAW::szTip) / sizeof(wchar_t));
}

}  // namespace

TrayApp::~TrayApp() {
    if (m_targetIcon != nullptr) {
        ::DestroyIcon(m_targetIcon);
        m_targetIcon = nullptr;
    }
    if (m_appIcon != nullptr) {
        ::DestroyIcon(m_appIcon);
        m_appIcon = nullptr;
    }
}

bool TrayApp::Initialize(HINSTANCE instance) {
    m_instance = instance;

    const int iconWidth = ::GetSystemMetrics(SM_CXSMICON);
    const int iconHeight = ::GetSystemMetrics(SM_CYSMICON);
    m_appIcon = LoadAppIcon(instance, iconWidth, iconHeight);
    if (m_appIcon == nullptr) {
        util::DebugLog(L"无法加载内置图标资源 IDI_ENTRAY");
        return false;
    }

    // 注册一次即可，Explorer 重启时会把这条消息广播给所有顶层窗口。
    m_taskbarCreatedMessage = ::RegisterWindowMessageW(L"TaskbarCreated");

    if (!CreateMessageWindow()) {
        return false;
    }
    if (!AddTrayIcon()) {
        return false;
    }

    // 恢复上次选择的程序；顺手修一下可能因 exe 被移动而失效的自启路径。
    const std::wstring saved = config::LoadTargetPath();
    if (!saved.empty()) {
        ApplyTarget(saved, false);
    }
    config::RefreshAutoStartPathIfNeeded();

    if (m_targetPath.empty()) {
        ShowBalloon(L"Entray 已就绪",
                    L"右键单击托盘里的这个图标，选择你想从此启动的程序。", NIIF_INFO);
    } else if (!TargetFileExists()) {
        const std::wstring text = ShortDisplayName() + L" 可能已被移动或删除，请右键重新选择。";
        ShowBalloon(L"找不到目标程序", text.c_str(), NIIF_WARNING);
    }
    return true;
}

bool TrayApp::CreateMessageWindow() {
    // 注意：必须用普通顶层窗口（只是不显示），不能用 HWND_MESSAGE，
    // 因为消息窗口收不到 TaskbarCreated 这种广播消息。
    WNDCLASSEXW windowClass;
    ::ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayApp::WindowProcThunk;
    windowClass.hInstance = m_instance;
    windowClass.lpszClassName = kWindowClassName;

    if (::RegisterClassExW(&windowClass) == 0) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            util::DebugLog(L"RegisterClassEx 失败：" + util::FormatWinError(error));
            return false;
        }
    }

    m_window = ::CreateWindowExW(0, kWindowClassName, L"Entray", WS_POPUP, 0, 0, 0, 0, nullptr,
                                 nullptr, m_instance, this);
    if (m_window == nullptr) {
        util::DebugLog(L"CreateWindowEx 失败：" + util::FormatWinError(::GetLastError()));
        return false;
    }
    return true;
}

bool TrayApp::AddTrayIcon() {
    ::ZeroMemory(&m_trayData, sizeof(m_trayData));
    m_trayData.cbSize = sizeof(m_trayData);
    m_trayData.hWnd = m_window;
    m_trayData.uID = kTrayIconId;
    m_trayData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_trayData.uCallbackMessage = kTrayMessage;
    m_trayData.hIcon = m_targetIcon != nullptr ? m_targetIcon : m_appIcon;
    ::lstrcpynW(m_trayData.szTip, ENTRAY_PRODUCT_NAME_W, TipBufferSize());

    if (::Shell_NotifyIconW(NIM_ADD, &m_trayData) == FALSE) {
        util::DebugLog(L"Shell_NotifyIcon(NIM_ADD) 失败：" + util::FormatWinError(::GetLastError()));
        return false;
    }
    UpdateToolTip();
    return true;
}

void TrayApp::RemoveTrayIcon() {
    ::Shell_NotifyIconW(NIM_DELETE, &m_trayData);
    m_trayData.hIcon = nullptr;
}

void TrayApp::ApplyTrayIcon() {
    m_trayData.hIcon = m_targetIcon != nullptr ? m_targetIcon : m_appIcon;
    m_trayData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    if (::Shell_NotifyIconW(NIM_MODIFY, &m_trayData) == FALSE) {
        // 图标有可能已经被 Explorer 重启弄丢了，重新添加一次。
        ::Shell_NotifyIconW(NIM_ADD, &m_trayData);
    }
}

void TrayApp::UpdateToolTip() {
    std::wstring tip;
    if (m_targetPath.empty()) {
        tip = L"Entray — 右键选择要启动的程序";
    } else {
        tip = L"Entray — " + ShortDisplayName();
        if (!TargetFileExists()) {
            tip += L"（找不到文件）";
        }
    }
    ::lstrcpynW(m_trayData.szTip, tip.c_str(), TipBufferSize());

    m_trayData.uFlags = NIF_TIP;
    if (::Shell_NotifyIconW(NIM_MODIFY, &m_trayData) == FALSE) {
        m_trayData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        ::Shell_NotifyIconW(NIM_ADD, &m_trayData);
    }
}

void TrayApp::ShowBalloon(const wchar_t* title, const wchar_t* text, DWORD icon) {
    if (m_window == nullptr) {
        return;
    }
    NOTIFYICONDATAW data = m_trayData;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = icon;
    data.uTimeout = 8000;
    ::lstrcpynW(data.szInfoTitle, title,
                static_cast<int>(sizeof(data.szInfoTitle) / sizeof(wchar_t)));
    ::lstrcpynW(data.szInfo, text, static_cast<int>(sizeof(data.szInfo) / sizeof(wchar_t)));
    if (::Shell_NotifyIconW(NIM_MODIFY, &data) == FALSE) {
        util::DebugLog(L"气泡提示发送失败");
    }
}

void TrayApp::ShowContextMenu() {
    const bool hasTarget = !m_targetPath.empty();
    const bool fileOk = hasTarget && TargetFileExists();
    const bool running = fileOk && TargetIsRunning();
    const std::wstring name = ShortDisplayName();

    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    std::wstring launchLabel = MakeMenuLabel(L"启动", name);
    if (hasTarget && !fileOk) {
        launchLabel += L"（文件已丢失）";
    }

    ::AppendMenuW(menu, MF_STRING | (fileOk ? MF_ENABLED : MF_GRAYED), IDM_LAUNCH,
                  launchLabel.c_str());
    ::AppendMenuW(menu, MF_STRING | (running ? MF_ENABLED : MF_GRAYED), IDM_CLOSE_TARGET,
                  MakeMenuLabel(L"关闭", name).c_str());
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_SELECT, L"选择程序…");
    ::AppendMenuW(menu, MF_STRING | (fileOk ? MF_ENABLED : MF_GRAYED), IDM_REVEAL,
                  L"打开所在文件夹");
    ::AppendMenuW(menu, MF_STRING | (hasTarget ? MF_ENABLED : MF_GRAYED), IDM_CLEAR,
                  L"清除已选程序");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING | (config::IsAutoStartEnabled() ? MF_CHECKED : MF_UNCHECKED),
                  IDM_AUTOSTART, L"开机自启");
    ::AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"关于 Entray");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    // 双击托盘 = 启动，菜单里把“启动”标成加粗的默认项。
    ::SetMenuDefaultItem(menu, IDM_LAUNCH, FALSE);

    POINT cursor;
    ::GetCursorPos(&cursor);
    ::SetForegroundWindow(m_window);
    ::TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, cursor.x, cursor.y, 0,
                     m_window, nullptr);
    // MSDN 要求的收尾动作：让菜单能正确响应后续的点击。
    ::PostMessageW(m_window, WM_NULL, 0, 0);
    ::DestroyMenu(menu);
}

void TrayApp::HandleCommand(int commandId) {
    switch (commandId) {
        case IDM_LAUNCH:
            OnLaunch();
            break;
        case IDM_CLOSE_TARGET:
            OnCloseTarget();
            break;
        case IDM_SELECT:
            OnChooseProgram();
            break;
        case IDM_REVEAL:
            OnRevealInExplorer();
            break;
        case IDM_CLEAR:
            ClearTarget();
            break;
        case IDM_AUTOSTART:
            OnToggleAutoStart();
            break;
        case IDM_ABOUT:
            OnAbout();
            break;
        case IDM_EXIT:
            ::DestroyWindow(m_window);
            break;
        default:
            break;
    }
}

void TrayApp::ApplyTarget(const std::wstring& path, bool persist) {
    m_targetPath = util::NormalizePath(path);
    m_executablePath = proc::ResolveExecutable(m_targetPath);
    m_lastLaunchedPid = 0;

    ReloadTargetIcon();
    ApplyTrayIcon();
    UpdateToolTip();

    if (persist) {
        config::SaveTargetPath(m_targetPath);
    }
}

void TrayApp::ClearTarget() {
    if (m_targetPath.empty()) {
        return;
    }
    m_targetPath.clear();
    m_executablePath.clear();
    m_lastLaunchedPid = 0;
    ReloadTargetIcon();
    ApplyTrayIcon();
    UpdateToolTip();
    config::ClearTargetPath();
    // 这里刻意不发通知：清除是个安静的清理动作，托盘提示文字已经能看出状态。
}

void TrayApp::ReloadTargetIcon() {
    if (m_targetIcon != nullptr) {
        ::DestroyIcon(m_targetIcon);
        m_targetIcon = nullptr;
    }
    if (!m_targetPath.empty()) {
        m_targetIcon = LoadTargetIcon(m_targetPath);
    }
}

void TrayApp::OnLaunch() {
    if (m_targetPath.empty()) {
        OnChooseProgram();
        return;
    }
    if (!TargetFileExists()) {
        const std::wstring message =
            L"找不到目标程序：\n" + m_targetPath + L"\n\n它可能已被移动或删除，请重新选择。";
        ::MessageBoxW(m_window, message.c_str(), L"Entray", MB_OK | MB_ICONWARNING);
        OnChooseProgram();
        return;
    }

    // 单击托盘时 Explorer 会连发 WM_LBUTTONUP 与 WM_LBUTTONDBLCLK，做个防抖。
    const ULONGLONG now = ::GetTickCount64();
    if (now - m_lastLaunchTick < kLaunchDebounceMs) {
        return;
    }
    m_lastLaunchTick = now;

    DWORD pid = 0;
    std::wstring error;
    if (!proc::Launch(m_targetPath, &pid, &error)) {
        ::MessageBoxW(m_window, error.c_str(), L"Entray", MB_OK | MB_ICONERROR);
        return;
    }
    m_lastLaunchedPid = pid;
    util::DebugLog(L"已启动 " + m_targetPath);
}

void TrayApp::OnCloseTarget() {
    if (m_executablePath.empty()) {
        return;
    }

    bool matchedByPath = false;
    std::vector<DWORD> pids = proc::FindRunning(m_executablePath, &matchedByPath);

    // 本程序亲自启动过的那个进程优先补进来，避免目标改由更新器拉起时漏掉。
    if (m_lastLaunchedPid != 0 && proc::IsProcessAlive(m_lastLaunchedPid) &&
        std::find(pids.begin(), pids.end(), m_lastLaunchedPid) == pids.end()) {
        pids.push_back(m_lastLaunchedPid);
    }

    const std::wstring name = DisplayName().empty() ? L"目标程序" : DisplayName();
    if (pids.empty()) {
        ShowBalloon(L"Entray", (name + L" 当前没有在运行。").c_str(), NIIF_INFO);
        return;
    }

    util::DebugLog(L"准备关闭 " + name + L"，匹配到 " + std::to_wstring(pids.size()) +
                   (matchedByPath ? L" 个进程（按完整路径匹配）" : L" 个进程（按文件名匹配）"));

    const std::vector<DWORD> stillAlive = proc::RequestClose(pids, kGracefulCloseTimeoutMs);
    const size_t closedCount = pids.size() - stillAlive.size();

    if (stillAlive.empty()) {
        // 关闭成功属于正常结果，不弹通知；需要留痕的话看调试日志。
        util::DebugLog(L"已关闭 " + name + L"，共 " + std::to_wstring(closedCount) + L" 个进程");
        m_lastLaunchedPid = 0;
        return;
    }

    std::wstring question = name;
    if (closedCount > 0) {
        question += L" 的部分进程已经退出，但还有 " +
                    std::to_wstring(static_cast<unsigned long>(stillAlive.size())) +
                    L" 个没有响应。";
    } else {
        question += L" 没有响应关闭请求，可能正在等待你保存内容。";
    }
    question += L"\n\n是否强制结束？未保存的数据会丢失。";

    if (::MessageBoxW(m_window, question.c_str(), L"Entray",
                      MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        const int forced = proc::ForceClose(stillAlive);
        util::DebugLog(L"已强制结束 " + name + L"，共 " + std::to_wstring(forced) + L" 个进程");
    }
    m_lastLaunchedPid = 0;
}

void TrayApp::OnChooseProgram() {
    std::vector<wchar_t> buffer(32768, L'\0');

    std::wstring initialDirectory = util::DirectoryOf(m_targetPath);
    if (initialDirectory.empty()) {
        initialDirectory = util::DesktopPath();
    }

    OPENFILENAMEW dialog;
    ::ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_window;
    dialog.lpstrFilter =
        L"程序 (*.exe;*.lnk;*.bat;*.cmd;*.com)\0*.exe;*.lnk;*.bat;*.cmd;*.com\0"
        L"所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = L"选择要从托盘启动的程序";
    dialog.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR |
                   OFN_HIDEREADONLY;

    if (::GetOpenFileNameW(&dialog) == FALSE) {
        return;
    }

    const std::wstring selected(buffer.data());
    if (selected.empty()) {
        return;
    }

    // 选好程序后不再弹通知：托盘图标、提示文字和菜单都会立刻反映新状态。
    ApplyTarget(selected, true);
}

void TrayApp::OnRevealInExplorer() {
    std::wstring target = m_executablePath;
    if (target.empty() || !util::FileExists(target)) {
        target = m_targetPath;
    }
    if (target.empty() || !util::FileExists(target)) {
        ::MessageBoxW(m_window, L"目标文件已经不存在了。", L"Entray", MB_OK | MB_ICONWARNING);
        return;
    }
    const std::wstring parameters = L"/select,\"" + target + L"\"";
    ::ShellExecuteW(m_window, L"open", L"explorer.exe", parameters.c_str(), nullptr, SW_SHOWNORMAL);
}

void TrayApp::OnToggleAutoStart() {
    const bool wasEnabled = config::IsAutoStartEnabled();
    if (!config::SetAutoStart(!wasEnabled)) {
        ::MessageBoxW(m_window, L"设置开机自启失败，可能被安全软件拦截了。", L"Entray",
                      MB_OK | MB_ICONWARNING);
        return;
    }
    // 开关状态由右键菜单的勾选项体现，不再额外弹通知。
    util::DebugLog(wasEnabled ? L"已关闭开机自启" : L"已开启开机自启");
}

void TrayApp::OnAbout() {
    std::wstring message;
    message += L"Entray " ENTRAY_VERSION_STRING_W L"\n";
    message += L"托盘快捷启动器\n\n";
    message += L"左键单击托盘图标：启动所选程序\n";
    message += L"右键单击托盘图标：打开菜单\n\n";
    message += L"菜单里可以：\n";
    message += L"  · 选择或更换要启动的程序（支持 .exe 与 .lnk 快捷方式）\n";
    message += L"  · 关闭所选程序\n";
    message += L"  · 开关开机自启\n\n";
    message += L"设置在注册表 HKEY_CURRENT_USER\\Software\\Entray 下，\n";
    message += L"删掉这个键就恢复成初始状态。\n\n";
    message += ENTRAY_HOMEPAGE_W;
    ::MessageBoxW(m_window, message.c_str(), L"关于 Entray", MB_OK | MB_ICONINFORMATION);
}

std::wstring TrayApp::DisplayName() const {
    if (m_targetPath.empty()) {
        return std::wstring();
    }
    std::wstring name = util::FileStemOf(m_targetPath);
    if (name.empty()) {
        name = util::FileNameOf(m_targetPath);
    }
    return name;
}

std::wstring TrayApp::ShortDisplayName() const {
    return util::Elide(DisplayName(), kMenuNameLimit);
}

bool TrayApp::TargetFileExists() const {
    return !m_targetPath.empty() && util::FileExists(m_targetPath);
}

bool TrayApp::TargetIsRunning() const {
    if (m_executablePath.empty()) {
        return false;
    }
    return proc::IsRunning(m_executablePath);
}

int TrayApp::Run() {
    MSG message = {};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK TrayApp::WindowProcThunk(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    TrayApp* self = reinterpret_cast<TrayApp*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr) {
        return self->WindowProc(window, message, wparam, lparam);
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

LRESULT TrayApp::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // Explorer 重启后会广播 TaskbarCreated，此时必须重新注册托盘图标。
    if (m_taskbarCreatedMessage != 0 && message == m_taskbarCreatedMessage) {
        if (::Shell_NotifyIconW(NIM_ADD, &m_trayData) == FALSE) {
            util::DebugLog(L"Explorer 重启后重新添加托盘图标失败");
        }
        ApplyTrayIcon();
        UpdateToolTip();
        return 0;
    }

    switch (message) {
        case kTrayMessage:
            switch (static_cast<UINT>(lparam)) {
                case WM_LBUTTONUP:
                case NIN_SELECT:
                    OnLaunch();
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowContextMenu();
                    break;
                default:
                    break;
            }
            return 0;

        case kSignalAlreadyRunning:
            ShowBalloon(L"Entray 已经在运行", L"请看通知区域里已有的那个图标。", NIIF_INFO);
            return 0;

        case WM_COMMAND:
            HandleCommand(static_cast<int>(LOWORD(wparam)));
            return 0;

        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            RemoveTrayIcon();
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace entray

// 托盘应用本体：隐藏消息窗口 + 托盘图标 + 右键菜单。
#ifndef ENTRAY_APP_H
#define ENTRAY_APP_H

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace entray {

class TrayApp {
public:
    TrayApp() = default;
    ~TrayApp();

    TrayApp(const TrayApp&) = delete;
    TrayApp& operator=(const TrayApp&) = delete;

    // 注册窗口类、创建隐藏窗口、注册托盘图标。失败返回 false。
    bool Initialize(HINSTANCE instance);

    // 消息循环。收到 WM_QUIT 后返回进程退出码。
    int Run();

private:
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

    bool CreateMessageWindow();
    bool AddTrayIcon();
    void RemoveTrayIcon();
    void ApplyTrayIcon();
    void UpdateToolTip();
    void ShowBalloon(const wchar_t* title, const wchar_t* text, DWORD icon);
    void ShowContextMenu();
    void HandleCommand(int commandId);

    // ---- 目标程序 ----
    void ApplyTarget(const std::wstring& path, bool persist);
    void ClearTarget();
    void ReloadTargetIcon();
    void OnLaunch();
    void OnCloseTarget();
    void OnChooseProgram();
    void OnRevealInExplorer();
    void OnToggleAutoStart();
    void OnAbout();

    std::wstring DisplayName() const;
    // 给菜单用的名字，超长时截断。
    std::wstring ShortDisplayName() const;
    bool TargetFileExists() const;
    bool TargetIsRunning() const;

    HINSTANCE m_instance = nullptr;
    HWND m_window = nullptr;
    NOTIFYICONDATAW m_trayData = {};
    UINT m_taskbarCreatedMessage = 0;

    // 用户选择的原始路径（可能是 .lnk）与解析后的可执行文件路径。
    std::wstring m_targetPath;
    std::wstring m_executablePath;

    // 最近一次由本程序启动的进程，关闭时优先使用它。
    DWORD m_lastLaunchedPid = 0;
    ULONGLONG m_lastLaunchTick = 0;

    // 自己拥有、需要 DestroyIcon 的图标。
    HICON m_appIcon = nullptr;
    HICON m_targetIcon = nullptr;
};

}  // namespace entray

#endif // ENTRAY_APP_H

// 全局常量：窗口类名、注册表位置、消息 ID 等。
#ifndef ENTRAY_COMMON_H
#define ENTRAY_COMMON_H

#include <windows.h>

namespace entray {

// 隐藏消息窗口的类名，同时用于查找已在运行的实例。
inline constexpr const wchar_t* kWindowClassName = L"Entray.MessageWindow";

// 单实例互斥体。Local\ 前缀限定在当前登录会话内，不影响同一台机器上的其他用户。
inline constexpr const wchar_t* kSingleInstanceMutexName = L"Local\\Entray.SingleInstance";

// 托盘图标回调消息。
inline constexpr UINT kTrayMessage = WM_APP + 1;

// 第二个实例被启动时，请求已运行实例弹一个气泡提示。
inline constexpr UINT kSignalAlreadyRunning = WM_APP + 2;

// 托盘图标 ID（同一个窗口可以注册多个图标，这里只需要一个）。
inline constexpr UINT kTrayIconId = 1;

// ---- 注册表位置（全部位于 HKEY_CURRENT_USER，无需管理员权限）----
inline constexpr const wchar_t* kAppRegistryPath = L"Software\\Entray";
inline constexpr const wchar_t* kTargetValueName = L"TargetPath";
inline constexpr const wchar_t* kRunRegistryPath =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr const wchar_t* kAutoStartValueName = L"Entray";

// 请求目标程序关闭后，最多等待多久（毫秒）；超时后询问用户是否强制结束。
inline constexpr int kGracefulCloseTimeoutMs = 2500;

// 连续两次点击托盘的防抖间隔（毫秒），避免双击时启动两次。
inline constexpr ULONGLONG kLaunchDebounceMs = 400;

}  // namespace entray

#endif // ENTRAY_COMMON_H

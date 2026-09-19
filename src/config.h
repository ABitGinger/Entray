// 配置持久化：目标程序路径 + 开机自启。
// 全部写在 HKEY_CURRENT_USER 下，不需要管理员权限。
#ifndef ENTRAY_CONFIG_H
#define ENTRAY_CONFIG_H

#include <string>

namespace config {

// 读取用户选择的程序路径（可能是 .lnk 快捷方式）。读不到返回空串。
std::wstring LoadTargetPath();
void SaveTargetPath(const std::wstring& path);
void ClearTargetPath();

bool IsAutoStartEnabled();
bool SetAutoStart(bool enabled);

// 开机自启项里记的是 Entray.exe 的绝对路径。用户把 exe 挪了位置以后这个值会失效，
// 每次启动时调用它做一次自愈。
void RefreshAutoStartPathIfNeeded();

}  // namespace config

#endif // ENTRAY_CONFIG_H

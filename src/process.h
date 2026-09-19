// 与“目标程序”有关的所有操作：解析快捷方式、启动、查找、关闭。
#ifndef ENTRAY_PROCESS_H
#define ENTRAY_PROCESS_H

#include <windows.h>

#include <string>
#include <vector>

namespace proc {

// 把 .lnk 解析成它指向的可执行文件路径；不是快捷方式或解析失败时返回归一化后的原路径。
std::wstring ResolveExecutable(const std::wstring& path);

// 用 ShellExecuteEx 启动目标，成功时通过 outPid 返回新进程的 PID（可能为 0，例如
// 目标只是激活了一个已在运行的实例）。
bool Launch(const std::wstring& path, DWORD* outPid, std::wstring* outError);

// 目标是否正在运行。
bool IsRunning(const std::wstring& exePath);

// 找出所有正在运行的、与 exePath 对应的进程。
// 先按完整镜像路径精确匹配；匹配不到时退回按可执行文件名匹配（会排除系统目录下的同名进程）。
// matchedByPath 回传是否用的精确匹配，便于界面给出更准确的提示。
std::vector<DWORD> FindRunning(const std::wstring& exePath, bool* matchedByPath);

bool IsProcessAlive(DWORD pid);

// 优雅关闭：给每个进程的所有顶层窗口发 WM_CLOSE，然后最多等 waitMs 毫秒。
// 返回等待结束后仍然活着的 PID。
std::vector<DWORD> RequestClose(const std::vector<DWORD>& pids, int waitMs);

// 强制结束进程，返回成功结束的个数。仅在用户确认后调用。
int ForceClose(const std::vector<DWORD>& pids);

}  // namespace proc

#endif // ENTRAY_PROCESS_H

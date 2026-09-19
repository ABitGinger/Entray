// 通用小工具：字符串、路径、错误信息。
#ifndef ENTRAY_UTIL_H
#define ENTRAY_UTIL_H

#include <windows.h>

#include <string>
#include <vector>

namespace util {

// ---- 字符串 ----
bool EqualsIgnoreCase(const std::wstring& lhs, const std::wstring& rhs);
std::wstring Trim(const std::wstring& text);
std::wstring TrimQuotes(const std::wstring& text);
std::wstring FileNameOf(const std::wstring& path);
std::wstring FileStemOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);
bool HasExtension(const std::wstring& path, const wchar_t* extension);
std::wstring Elide(const std::wstring& text, size_t maxChars);

// ---- 路径 ----
// 去引号 -> 展开环境变量 -> 转绝对路径 -> 去掉 \\?\ 前缀和结尾反斜杠。
std::wstring NormalizePath(const std::wstring& path);
std::wstring ExpandEnvironment(const std::wstring& path);
std::wstring GetModulePath(HMODULE module = nullptr);
bool IsUnderWindowsDirectory(const std::wstring& fullPath);
std::wstring DesktopPath();

// ---- 文件 ----
bool FileExists(const std::wstring& path);

// ---- 杂项 ----
std::wstring FormatWinError(DWORD code);
void DebugLog(const std::wstring& message);

}  // namespace util

#endif // ENTRAY_UTIL_H

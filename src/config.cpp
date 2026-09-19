#include "config.h"

#include <windows.h>

#include <vector>

#include "common.h"
#include "util.h"

namespace config {
namespace {

// 读一个 REG_SZ 值。不存在或类型不符时返回空串。
std::wstring ReadStringValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return std::wstring();
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = ::RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes == 0) {
        ::RegCloseKey(key);
        return std::wstring();
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    bytes = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    status = ::RegQueryValueExW(key, valueName, nullptr, &type,
                                reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::wstring();
    }

    // 兜底：确保以 NUL 结尾，再把结尾的 NUL 去掉。
    buffer.back() = L'\0';
    return std::wstring(buffer.data());
}

bool WriteStringValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName,
                      const std::wstring& value) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = ::RegSetValueExW(key, valueName, 0, REG_SZ,
                                            reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool DeleteValue(HKEY root, const wchar_t* subKey, const wchar_t* valueName) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, subKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = ::RegDeleteValueW(key, valueName);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

// 自启命令必须带引号，否则路径里有空格（例如 C:\Program Files\...）会启动失败。
std::wstring BuildAutoStartCommand() {
    const std::wstring exePath = util::GetModulePath();
    if (exePath.empty()) {
        return std::wstring();
    }
    return L"\"" + exePath + L"\"";
}

}  // namespace

std::wstring LoadTargetPath() {
    return ReadStringValue(HKEY_CURRENT_USER, entray::kAppRegistryPath, entray::kTargetValueName);
}

void SaveTargetPath(const std::wstring& path) {
    if (path.empty()) {
        ClearTargetPath();
        return;
    }
    if (!WriteStringValue(HKEY_CURRENT_USER, entray::kAppRegistryPath, entray::kTargetValueName,
                          path)) {
        util::DebugLog(L"保存目标路径失败");
    }
}

void ClearTargetPath() {
    // 只删这一个值，不再像老版本那样把整个键删掉重建，避免误伤同键下的其他设置。
    DeleteValue(HKEY_CURRENT_USER, entray::kAppRegistryPath, entray::kTargetValueName);
}

bool IsAutoStartEnabled() {
    const std::wstring command = ReadStringValue(HKEY_CURRENT_USER, entray::kRunRegistryPath,
                                                 entray::kAutoStartValueName);
    return !command.empty();
}

bool SetAutoStart(bool enabled) {
    if (!enabled) {
        return DeleteValue(HKEY_CURRENT_USER, entray::kRunRegistryPath,
                           entray::kAutoStartValueName);
    }
    const std::wstring command = BuildAutoStartCommand();
    if (command.empty()) {
        return false;
    }
    return WriteStringValue(HKEY_CURRENT_USER, entray::kRunRegistryPath,
                            entray::kAutoStartValueName, command);
}

void RefreshAutoStartPathIfNeeded() {
    const std::wstring expected = BuildAutoStartCommand();
    if (expected.empty()) {
        return;
    }
    const std::wstring current = ReadStringValue(HKEY_CURRENT_USER, entray::kRunRegistryPath,
                                                 entray::kAutoStartValueName);
    if (current.empty()) {
        return;  // 用户没开自启，别擅自加上
    }
    if (!util::EqualsIgnoreCase(current, expected)) {
        util::DebugLog(L"修正开机自启路径：" + expected);
        WriteStringValue(HKEY_CURRENT_USER, entray::kRunRegistryPath, entray::kAutoStartValueName,
                         expected);
    }
}

}  // namespace config

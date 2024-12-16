#include <windows.h>
#include <shellapi.h>
#include <winreg.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1
#define ID_EXIT     2000
#define ID_SELECT   2001
#define ID_AUTOSTART 2002

// 添加以下宏定义来支持 Unicode
#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

HINSTANCE g_hInst = NULL;
HWND g_hwnd = NULL;
NOTIFYICONDATA g_nid = {0};
TCHAR g_targetPath[MAX_PATH] = TEXT("");

// 在全局变量声明后添加新函数
void UpdateTrayIcon(const TCHAR* programPath) {
    // 保存旧图标以便删除
    HICON oldIcon = g_nid.hIcon;
    
    // 从程序文件中提取图标
    HICON newIcon = NULL;
    ExtractIconEx(programPath, 0, NULL, &newIcon, 1);
    
    // 如果无法提取图标，使用默认图标
    if (!newIcon) {
        newIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    
    // 更新托盘图标
    g_nid.hIcon = newIcon;
    Shell_NotifyIcon(NIM_MODIFY, &g_nid);
    
    // 删除旧图标
    if (oldIcon) {
        DestroyIcon(oldIcon);
    }
}

// 在全局变量声明后添加这些函数
void SaveTargetPath(const TCHAR* path) {
    HKEY hKey;
    // 先删除已有的键，确保清理旧数据
    RegDeleteKey(HKEY_CURRENT_USER, TEXT("Software\\Entray"));
    
    // 重新创建键并保存新值
    if (RegCreateKeyEx(HKEY_CURRENT_USER, 
        TEXT("Software\\Entray"), 
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        
        // 写入新路径
        RegSetValueEx(hKey, TEXT("TargetPath"), 0, REG_SZ,
            (BYTE*)path, (lstrlen(path) + 1) * sizeof(TCHAR));
        
        RegCloseKey(hKey);
    }
}

void LoadTargetPath() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
        TEXT("Software\\Entray"),
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        DWORD type = REG_SZ;
        DWORD size = MAX_PATH * sizeof(TCHAR);
        
        // 读取路径
        if (RegQueryValueEx(hKey, TEXT("TargetPath"), NULL, &type,
            (BYTE*)g_targetPath, &size) == ERROR_SUCCESS) {
            
            // 验证文件是否存在
            WIN32_FIND_DATA findData;
            HANDLE hFind = FindFirstFile(g_targetPath, &findData);
            if (hFind == INVALID_HANDLE_VALUE) {
                // 文件不存在，清除路径并删除注册表项
                g_targetPath[0] = '\0';
                RegCloseKey(hKey);
                RegDeleteKey(HKEY_CURRENT_USER, TEXT("Software\\Entray"));
            } else {
                FindClose(hFind);
                // 文件存在，更新托盘图标
                UpdateTrayIcon(g_targetPath);
            }
        }
        
        RegCloseKey(hKey);
    }
}

// 检查是否已设置开机自启
bool IsAutoStartEnabled() {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
        TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        
        TCHAR path[MAX_PATH];
        DWORD size = sizeof(path);
        DWORD type = REG_SZ;
        
        LONG result = RegQueryValueEx(hKey, TEXT("Entray"), NULL, &type,
            (BYTE*)path, &size);
            
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

// 设置或取消开机自启
void ToggleAutoStart(HWND hwnd) {
    HKEY hKey;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,
        TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        
        if (IsAutoStartEnabled()) {
            // 取消开机自启
            if (RegDeleteValue(hKey, TEXT("Entray")) == ERROR_SUCCESS) {
                // MessageBox(hwnd, TEXT("已取消开机自启"), TEXT("提示"), MB_OK | MB_ICONINFORMATION);
            }
        } else {
            // 设置开机自启
            TCHAR exePath[MAX_PATH];
            GetModuleFileName(NULL, exePath, MAX_PATH);
            
            if (RegSetValueEx(hKey, TEXT("Entray"), 0, REG_SZ,
                (BYTE*)exePath, (lstrlen(exePath) + 1) * sizeof(TCHAR)) == ERROR_SUCCESS) {
                // MessageBox(hwnd, TEXT("已设置开机自启"), TEXT("提示"), MB_OK | MB_ICONINFORMATION);
            }
        }
        
        RegCloseKey(hKey);
    }
}

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 创建托盘图标菜单
void CreatePopupMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_SELECT, TEXT("选择程序"));
    
    // 添加开机自启动选项
    bool isAutoStart = IsAutoStartEnabled();
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_AUTOSTART,
        isAutoStart ? TEXT("取消开机自启") : TEXT("开机自启"));
    
    InsertMenu(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, 3, MF_BYPOSITION | MF_STRING, ID_EXIT, TEXT("退出"));
    
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, 
                   pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    g_hInst = hInstance;

    // 注册窗口类
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = TEXT("TrayIconApp");
    
    if (!RegisterClassEx(&wc)) return 1;

    // 创建隐藏窗口
    g_hwnd = CreateWindow(TEXT("TrayIconApp"), TEXT(""),
                         WS_OVERLAPPED, 0, 0, 0, 0,
                         NULL, NULL, hInstance, NULL);

    if (!g_hwnd) return 2;

    ShowWindow(g_hwnd, SW_HIDE);
    UpdateWindow(g_hwnd);

    // 初始化托盘图标
    ZeroMemory(&g_nid, sizeof(NOTIFYICONDATA));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = ID_TRAYICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    
    if (!g_nid.hIcon) {
        DestroyWindow(g_hwnd);
        return 3;
    }

    lstrcpyn(g_nid.szTip, TEXT("快捷启动器"), sizeof(g_nid.szTip)/sizeof(TCHAR));

    if (!Shell_NotifyIcon(NIM_ADD, &g_nid)) {
        DestroyIcon(g_nid.hIcon);
        DestroyWindow(g_hwnd);
        return 4;
    }

    Shell_NotifyIcon(NIM_MODIFY, &g_nid);
    
    // 加载上次保存的程序路径
    LoadTargetPath();

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理托盘图标
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    DestroyIcon(g_nid.hIcon);

    // 如果保存的路径已经无效���清理注册表
    if (g_targetPath[0] == '\0') {
        RegDeleteKey(HKEY_CURRENT_USER, TEXT("Software\\Entray"));
    }

    return static_cast<int>(msg.wParam);
}

// 窗口过程函数实现
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                if (g_targetPath[0] != '\0') {
                    ShellExecute(NULL, TEXT("open"), g_targetPath, 
                                NULL, NULL, SW_SHOWNORMAL);
                } else {
                    MessageBox(hwnd, TEXT("请先右键选择要启动的程序"), 
                             TEXT("提示"), MB_OK | MB_ICONINFORMATION);
                }
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                CreatePopupMenu(hwnd, pt);
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_EXIT:
                    DestroyWindow(hwnd);
                    break;
                    
                case ID_SELECT: {
                    OPENFILENAME ofn = {0};
                    TCHAR szFile[MAX_PATH] = TEXT("");
                    
                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = TEXT("可执行文件\0*.exe\0所有文件\0*.*\0");
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    
                    if (GetOpenFileName(&ofn)) {
                        lstrcpyn(g_targetPath, szFile, MAX_PATH);
                        // 更新托盘图标
                        UpdateTrayIcon(g_targetPath);
                        // 保存选择的路径
                        SaveTargetPath(g_targetPath);
                    }
                    break;
                }
                case ID_AUTOSTART:
                    ToggleAutoStart(hwnd);
                    break;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
} 
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1
#define ID_EXIT     2000
#define ID_SELECT   2001

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

// 窗口过程函数声明
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// 创建托盘图标菜单
void CreatePopupMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_SELECT, TEXT("选择程序"));
    InsertMenu(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_EXIT, TEXT("退出"));
    
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

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理托盘图标
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
    DestroyIcon(g_nid.hIcon);
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
                    }
                    break;
                }
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
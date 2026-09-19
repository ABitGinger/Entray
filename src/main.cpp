// Entray 入口点。真正的逻辑都在 app.h / app.cpp 里。
#include <windows.h>

#include <objbase.h>

#include "app.h"
#include "common.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/, LPWSTR /*commandLine*/,
                    int /*showCommand*/) {
    // 单实例：重复双击 exe 时不该出现第二个托盘图标。
    // 互斥体句柄在整个进程生命周期内一直持有，退出时由系统自动释放。
    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, entray::kSingleInstanceMutexName);
    if (mutex != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = ::FindWindowW(entray::kWindowClassName, nullptr);
        if (existing != nullptr) {
            ::PostMessageW(existing, entray::kSignalAlreadyRunning, 0, 0);
        }
        ::CloseHandle(mutex);
        return 0;
    }

    // 解析 .lnk 需要 COM。失败也不影响主流程，只是快捷方式可能解析不出来。
    const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int exitCode = 0;
    {
        entray::TrayApp app;
        if (app.Initialize(instance)) {
            exitCode = app.Run();
        } else {
            exitCode = 1;
            ::MessageBoxW(nullptr,
                          L"Entray 启动失败：无法创建托盘图标。\n\n"
                          L"请确认系统托盘（通知区域）没有被完全禁用，然后重试。",
                          L"Entray", MB_OK | MB_ICONERROR);
        }
    }

    if (SUCCEEDED(comResult)) {
        ::CoUninitialize();
    }
    if (mutex != nullptr) {
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
    }
    return exitCode;
}

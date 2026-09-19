// 资源 ID 与菜单命令 ID。
// 本文件同时被 C++ 源码和 resources/app.rc 引用，因此只能包含纯宏定义。
#ifndef ENTRAY_RESOURCE_H
#define ENTRAY_RESOURCE_H

// ---- 图标 ----
#define IDI_ENTRAY 101

// ---- 托盘菜单命令 ----
#define IDM_LAUNCH       40001
#define IDM_CLOSE_TARGET 40002
#define IDM_SELECT       40003
#define IDM_REVEAL       40004
#define IDM_CLEAR        40005
#define IDM_AUTOSTART    40006
#define IDM_ABOUT        40007
#define IDM_EXIT         40008

#endif // ENTRAY_RESOURCE_H

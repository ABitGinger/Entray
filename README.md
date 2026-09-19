# Entray

> 在系统托盘里放一个图标，用它打开你指定的任何程序。

单文件、免安装、不写系统盘以外的地方。选中一次就会一直记住，下次开机还在。

---

## 它能干什么

| 能力 | 说明 |
| --- | --- |
| **一键启动** | 左键单击托盘图标即可启动所选程序 |
| **图标跟随** | 托盘图标自动变成所选程序的图标，一眼就认得出 |
| **关闭所选程序** | 右键菜单里直接关掉它——会先礼貌请求退出，卡住了才问你要不要强制结束 |
| **记住选择** | 选择结果存在注册表 `HKCU\Software\Entray`，重启电脑也不用重选 |
| **开机自启** | 右键菜单里一个勾选项，不需要管理员权限 |
| **快捷方式也能选** | 直接选桌面上的 `.lnk` 就行，会自动解析到真正的 exe |
| **单实例** | 手滑点开两次也不会冒出两个托盘图标 |

## 下载与使用

1. 到 [Releases](https://github.com/ABitGinger/Entray/releases) 下载 `Entray.exe`
2. **双击打开**，不需要安装
3. 屏幕右下角出现一个绿色图标，**右键它** → `选择程序…`，选中你要启动的程序或它的快捷方式

之后每次想启动它，左键单击托盘图标就行。

## 用例

### 一、取消微信闪烁

选择**你桌面上的"微信"**快捷方式，或者微信本体 `C:\Program Files\Tencent\WeChat\WeChat.exe`，托盘里就会出现一个不会闪烁的微信图标。把微信原本的图标隐藏掉，你就得到了一个**不会闪的微信**。

![entray-wx](https://github.com/user-attachments/assets/50de57b5-150c-4d77-94ac-6d7f177fbea9)

### 二、"开机自启"，但不挂后台

例如选择百度网盘的主程序或它的快捷方式：开机后它的图标会直接出现在托盘里，但只有你真正点开它，它才开始运行、才开始占用内存。

## 系统要求

- Windows 7 / 8 / 8.1 / 10 / 11（x64）
- **不需要安装 .NET、不需要 VC++ 运行库**——运行时已经静态链接进 exe 了

## 常见问题

**Q：以前下载的版本双击没反应，或者提示"缺少 VCRUNTIME140.dll / MSVCP140.dll"？**

那是旧版本打包方式的问题，v2.0.0 已经修好了。旧版用 CMake 默认的 `/MD` 动态链接 CRT，还错误地发布了 Debug 版本，所以那台电脑上只要没装 Visual Studio 或 VC++ 运行库就跑不起来。现在改成 `/MT` 静态链接，运行时全部编进 exe，并且 CI 里有一道自动化关卡专门检查这件事。

**Q：Windows 提示"已保护你的电脑"/"未知发布者"？**

因为 exe 没有代码签名证书，这是 SmartScreen 的正常反应。点"更多信息 → 仍要运行"即可。

**Q：想彻底卸载？**

退出程序：右键托盘图标 → `退出`。想连设置一起清掉，删除注册表里的 `HKEY_CURRENT_USER\Software\Entray` 键，并在 `HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Run` 里删掉 `Entray` 这一项（或者在菜单里取消勾选"开机自启"）。

## 从源码构建

需要 Visual Studio 2022（勾选"使用 C++ 的桌面开发"）和 CMake 3.20+。

```powershell
# 最简单的方式：一把梭，编译 + 自检 + 输出到 dist\Entray.exe
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

也可以直接走 CMake：

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
# 产物在 build\Release\Entray.exe

# 顺手验证一下它真的不依赖外部运行库
python tools\check_exe.py build\Release\Entray.exe
```

打包成 zip：

```powershell
cpack --preset msvc-release
```

## 项目结构

```
src/
  main.cpp        入口：单实例互斥体、COM 初始化
  app.cpp/.h      托盘应用本体：隐藏窗口、托盘图标、右键菜单、各项命令
  process.cpp/.h  目标程序的解析（.lnk）、启动、查找、关闭
  config.cpp/.h   注册表读写：目标路径、开机自启
  util.cpp/.h     字符串 / 路径 / 错误信息小工具
  common.h        全局常量（窗口类名、注册表位置、消息 ID）
  resource.h      资源与菜单命令 ID
  version.h       版本号（C++ 与 .rc 共用，唯一来源）
resources/
  app.rc          图标 + 应用程序清单 + 版本信息，全部编译进 exe
  app.manifest    DPI 感知、asInvoker、v6 通用控件
  icon.ico        多尺寸图标
tools/
  make_icon.py    图标生成脚本（改设计就改它重跑）
  check_exe.py    产物校验：确认单文件、无运行库依赖、资源齐全
scripts/
  build.ps1       一键构建
```

技术细节和设计取舍见 [开发文档.md](开发文档.md)。

## 许可

见 [LICENSE](LICENSE)。

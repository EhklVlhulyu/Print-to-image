# 第三方组件与许可声明 / Third-Party Notices

本项目以 **Microsoft Public License (MS-PL)** 发布，完整许可文本见仓库根目录的 `LICENSE`。

## 1. Microsoft windows-driver-samples（MS-PL）

以下文件源自 Microsoft 的 [windows-driver-samples](https://github.com/microsoft/windows-driver-samples)
仓库 `print/XpsRasFilter` 示例，版权归 Microsoft Corporation，按 **MS-PL** 授权。
这些文件保留其原始版权头（`Copyright (c) Microsoft Corporation. All rights reserved`），
本次仅作必要的本地化修改（去掉 WDK/WPP 依赖、改用本项目的光栅化与编码流程）。

> **来源版本**：主分支上已看不到该示例 —— 它在提交
> `cbbd53eda55fbdf65f22b5f57c7dbc4b1ea594fd`（2023-08-10，PR #1017 "Remove Print Driver Samples"）
> 中被删除，目的就是引导使用者转向 Print Support App (PSA)。
> 本项目代码取自删除前的快照
> [`ba69fa3c072abdb56029a324f0e6e23eea1b33d3`](https://github.com/microsoft/windows-driver-samples/tree/ba69fa3c072abdb56029a324f0e6e23eea1b33d3/print/XpsRasFilter)
> （2022-05-24，分支 `win11-22h2`）。
> 仓库根目录的 `LICENSE` 在该快照与当前主分支上**都是 MS-PL**（`Copyright (c) 2015 Microsoft`），
> 所以删除动作不改变这些文件的授权。


| 文件 | 与原示例的关系 |
| --- | --- |
| `src/Exception.cpp`、`src/Exception.h` | 原样保留 |
| `src/OMConvertor.cpp`、`src/OMConvertor.h` | 原样保留（IFixedPage → IXpsOMPage 转换） |
| `src/PThandler.cpp`、`src/PThandler.h` | 原样保留（打印票据解析） |
| `src/UnknownBase.h` | 原样保留（COM 引用计数基类） |
| `src/filtertypes.h` | 基本原样（删除 TIFF 相关类型与未使用的 typedef，补充 `xpsDPI` 常量） |
| `src/xpsrasfilter.cpp`、`src/xpsrasfilter.h` | 改编（类名改为 `PtImageFilter`，移除 WPP 追踪，接入 `ImageWriter`） |
| `src/rasinterface.cpp`、`src/rasinterface.h` | 改编（原始示例的 `rasinterface` + `BitmapHandler` 已被替换为按 WIC 编码、多页拆分、白色背景合成的实现） |

其余文件（`src/precomp.h`、`src/WppTrace.h`、`src/dllentry.cpp`、`src/ptimageres.rc`、
`driver/*`、`scripts/*`、`installer/*`、`build/*`）为本项目原创。

MS-PL 第 3(C) 条要求分发时保留软件中已有的版权、专利、商标与归属声明；
第 3(D) 条要求以源码形式分发时附带完整许可文本（见 `LICENSE`），
以编译形式分发时须采用与该许可相容的许可 —— 本项目整体以 MS-PL 分发，因此
`ptimagefilter.dll` / `ptimageres.dll` 的二进制分发同样满足该条。

## 2. NSIS（zlib/libpng License）

安装程序使用 [NSIS 3.10](https://nsis.sourceforge.io/) 编译。NSIS 采用 zlib/libpng 许可，
允许自由使用、修改与再分发（含商业用途），对生成的安装程序不附加署名义务。
编译器仅放在 `tools/nsis/nsis-3.10`（不随发行包分发、已在 `.gitignore` 中忽略）。

## 3. Microsoft Windows SDK / ATL / MSVC 运行库

驱动与安装程序构建时使用 Windows SDK 10.0.26100.0 头文件与 ATL 头文件，
并以 `/MT` 静态链接 MSVC 运行库。这些组件仅在编译期使用，不随本项目源码分发；
其使用受 Visual Studio 许可条款约束。运行期依赖的 `XpsRasterService.dll`、
`prntvpt.dll`、`windowscodecs.dll` 等均为 Windows 自带系统组件，本项目不分发它们。

## 4. 商标与非关联声明

本项目是独立的第三方实现，**与 Microsoft Corporation 无任何隶属、赞助或背书关系**。
"Microsoft"、"Windows"、"XPS" 等名称是其各自权利人的商标，此处仅用于描述兼容性
（指示性合理使用）。本项目未使用任何 Microsoft 徽标或图形商标。
打印机名称 "Print to image" 为本项目自用名称。

## 5. 测试数据

`build\tests\data\openxps-page.fpage` 是本机由 Microsoft XPS Document Converter (MXDC)
生成的单页标记（内含其自动生成的注释行），仅用于回归测试，不含字体、图片或其他受版权保护的内容。

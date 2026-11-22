# 说明：本项目由AI编写，不保证其兼容性及稳定性
以下是DeepSeek撰写的项目介绍
# Print to image — Windows 虚拟图片打印机驱动

一个 v4 打印机驱动：安装后在“打印机和扫描仪”里出现名为 **Print to image** 的虚拟打印机，
打印时由系统弹出「打印输出另存为」对话框，可把任意文档存成 **JPEG / PNG / BMP** 图片。

- 目标系统：理论下限 **Windows 8（6.2.9000）及以上**
- 架构：**x64 + x86**（安装包同时携带两套驱动二进制）
- 端口类型：`PORTPROMPT:`（文件保存端口，由 Spooler 弹出保存对话框）
- 驱动模型：v4 驱动（`ClassVer=4.0`）+ PrintDeviceCapabilities（PDC）描述文件
- 渲染组件：XPSDrv 渲染过滤器 `ptimagefilter.dll`（XPS → WIC 编码 → 图片文件）
- 安装程序：NSIS，单文件 `.exe`，自带卸载程序，**不依赖任何脚本或 Node.js**

## 功能

| 项目 | 说明 |
| --- | --- |
| 输出格式 | JPEG（质量 0.9）、PNG、BMP，在“另存为”对话框里按扩展名选择 |
| 分辨率 | 96 / 150 / 300 / 600 DPI（打印首选项里选，默认 300） |
| 纸张 | A4、A5、A3、Letter、Legal、Tabloid、Executive、Statement、JIS B4、B5 |
| 多页文档 | 每页一张图片：第 1 页用你选的文件名，之后自动 `-2`、`-3`…|
| 背景 | 统一合成到白色背景（透明区域不会变黑） |
| 取消 | 从打印队列取消作业时，过滤器通过 `IXpsRasterizerNotificationCallback` 及时退出 |

## 目录结构

```
ptimage/
├─ build/
│  ├─ build.cmd              # 编译过滤器 DLL 与资源 DLL（MSVC + Windows SDK）
│  ├─ test.cmd               # 编译并运行离线自测程序
│  ├─ tests/testwriter.cpp   # 假光栅化器下测试编码/命名/发布/白底/标记解析
│  ├─ tests/data/openxps-page.fpage  # MXDC 产生的真实页面标记（回归用例输入，随源码提交）
│  ├─ tests/print-gdi.ps1            # 用 PrintToFile 打印一页/多页（验收脚本内部使用）
│  └─ tests/verify-driver.ps1        # 装好后端到端验收（普通权限）
├─ LICENSE                          # MS-PL 全文（本项目整体许可）
├─ THIRD-PARTY-NOTICES.md           # 第三方组件、派生文件清单与商标声明
├─ ptimage.sln                      # Visual Studio 解决方案（filter / res / testwriter）
├─ driver/
│  ├─ ptimage.inf                    # v4 安装信息（UTF-16LE）
│  ├─ ptimage-manifest.ini           # v4 清单：[DriverConfig] [FileSave] [DriverRender]
│  ├─ ptimage-pdc.xml                # PrintDeviceCapabilities（纸张/方向/分辨率/颜色）
│  └─ ptimage-pipelineconfig.xml     # 过滤器管线：ptimagefilter.dll + XpsRasterService
├─ scripts/
│  ├─ build-package.ps1      # 编译 → 组包 → 生成 SHA-256 目录 → 自签名
│  └─ check-package.ps1      # 静态校验：文件清单、GUID 一致、资源字符串、目录签名
├─ installer/
│  ├─ ptimage.nsi           # NSIS 脚本：安装 + 卸载全部逻辑都在这里
│  ├─ LICENSES.txt          # 安装向导的许可页 + 安装目录里的许可文本
│  └─ build-nsis.ps1        # 组包 + 调用 makensis 生成 dist\print-to-images-1.0.0-setup.exe
├─ tools/
│  ├─ ptimageinstall/ptimageinstall.cpp  # 安装辅助程序：Win32 API 注册驱动 + 建队列（不依赖 PowerShell）
│  └─ nsis/nsis-3.10/       # NSIS 3.10 便携版（构建机用，不随发行包分发）
├─ dist/
│  └─ print-to-images-1.0.0-setup.exe   # 最终交付物（唯一发行文件）
└─ src/
   ├─ xpsrasfilter.cpp/.h    # IPrintPipelineFilter 实现（IXpsDocumentProvider → 图片）
   ├─ rasinterface.cpp/.h    # XPS 栅格化 + WIC 编码 + 白底合成 + 发布
   ├─ OMConvertor.cpp/.h     # IFixedPage → IXpsOMPage（含资源）
   ├─ PThandler.cpp/.h       # 从 PrintTicket 读取 DPI 等参数
   ├─ dllentry.cpp           # DllMain / DllGetClassObject / DllCanUnloadNow
   ├─ ptimageres.rc          # 资源字符串（100..102 扩展名，200 对话框标题）
   └─ Exception.*, filtertypes.h, UnknownBase.h, precomp.h, WppTrace.h
```

**驱动包共 11 个文件**：`ptimagefilter.dll`、`ptimageres.dll`、`ptimage.inf`、`ptimage_x86.inf`、`ptimage.cat`、
`ptimage.cer`、`ptimage-manifest.ini`、`ptimage-pdc.xml`、`ptimage-pipelineconfig.xml`，
外加 32 位版本 `x86\ptimagefilter.dll`、`x86\ptimageres.dll`。
不含安装/卸载脚本，也不含文档——安装与卸载逻辑全部编译进 `print-to-images-1.0.0-setup.exe`。

## 编译

**环境要求**

- Windows 10 build 19041+ / Windows 11（构建机需为 x64 且装有 32 位 C++ 工具集）
- 一次 `build\build.cmd` 同时产出 x64 与 x86 两套二进制
- Visual Studio 2019 / 2022 / 2026（含 C++ 桌面开发负载）
- Windows SDK 10（`filterpipeline.h`、`xpsrassvc.h`、`xpsobjectmodel.h`、`windowscodecs`、`shlwapi`）
- 不需要 WDK 的构建系统：直接用 `cl.exe` + `link.exe`

```bat
build\build.cmd
```

产物：`build\ptimagefilter.dll`、`build\ptimageres.dll`。

### 用 Visual Studio 解决方案编译

`ptimage.sln` 把三个项目组织在一起，日常改代码/调试可以直接用它：

| 项目 | 类型 | 说明 |
| --- | --- | --- |
| `ptimagefilter` | DLL | 渲染过滤器（x64 与 Win32） |
| `ptimageres` | 资源 DLL | “另存为”对话框里的文件类型字符串 |
| `testwriter` | 控制台 EXE | 离线自测程序（假光栅化器） |

```bat
:: 命令行：直接调 MSBuild（x64 Release）
msbuild ptimage.sln -p:Configuration=Release -p:Platform=x64
```

- 解决方案平台为 `x64` 与 `x86`；`x86` 映射到项目平台 `Win32`，因此 32 位产物落在 `build\vs\Win32\<Config>\`，64 位落在 `build\vs\x64\<Config>\`。
- 项目使用 `v145` 工具集（VS 2026）；用旧版 VS 打开时会提示重定目标，同意即可。
- `testwriter.exe` 的调试工作目录已设为 `build\`，可直接 F5 运行自测。
- **打包仍以 `build\build.cmd` 为准**：它产出的 `build\*.dll` / `build\x86\*.dll` 才是 `scripts\build-package.ps1` 使用的输入（VS 输出会额外嵌入清单资源，体积略大）。

## 自测

```bat
build\test.cmd
```

用假的光栅化器驱动 `ImageWriter`，覆盖：

- PNG / JPEG / BMP 编码、DPI 元数据、多页命名（`out-png-2.png`、`out-png-3.png`）；
- 白底合成：透明区必须变成纯白，已绘制像素颜色不变；
- `-- page markup` 回归用例：读取随源码提交的真实 MXDC 页面标记
  （`build\tests\data\openxps-page.fpage`），用页面原本的
  `http://schemas.openxps.org/oxps/v1.0` 命名空间解析必须得到 `0x8052000C`
  （`XPS_E_INVALID_MARKUP`），换成经典 XPS 命名空间后必须成功 —— 0 字节问题的原因固化。

预期结尾：`0 failure(s)` / `test complete`。

## 打包与签名

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-package.ps1
```

依次完成：编译（x64 + x86）→ 复制 11 个文件到 `build\package\`（含 `x86\`）→ `New-FileCatalog`（SHA-256）
→ 复用或新建自签名证书（`CN=Print to image self-signed driver`）→ `signtool` 签名 `ptimage.cat`。

可选参数：`-SkipBuild`（复用已有 DLL）、`-SkipSign`（只组包不签名）。

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check-package.ps1
```

静态校验：11 个文件齐全且**没有多余文件**、INF 文件清单与磁盘一致、`ClassVer=4.0`、
两个**纯**架构 INF（`ptimage.inf` 只含 `NTamd64`、`ptimage_x86.inf` 只含 `NTx86`，都不带 OS 版本修饰）与 `DriverVer=…,1.0.0`、
`PnpLockdown`、Printer 类 GUID、无 v3 专用指令、`PrinterDriverID` 两处一致、
过滤器 CLSID 与 `src\dllentry.cpp` 一致、`FilterServiceProvider=XpsRasterService.dll`、
`[FileSave]` 只有 jpg/png/bmp 且各自独立资源 ID、`XpsFormat=XPS`、
资源字符串 100/101/102/200 可从 `ptimageres.dll` 读出、目录文件覆盖 6 个文件且已签名。

## 制作安装程序（NSIS）

```powershell
powershell -ExecutionPolicy Bypass -File installer\build-nsis.ps1
```

产物：`dist\print-to-images-1.0.0-setup.exe`（约 250 KB，内含 x64 + x86 两套 DLL）。

- payload 只有 `build\package\` 的 11 个文件（含 `x86\` 子目录与两个架构 INF）；
- 构建时从 `ptimage.cer` 读出证书指纹，用 `/DCERTTHUMB=` 传给 `makensis`，
  卸载程序据此精确删除证书；
- NSIS 为 32 位程序，安装/卸载全程用 `${DisableX64FSRedirection}` 关闭 WOW64 重定向，
  所有外部命令都用 `$SYSDIR` 绝对路径调用。

| 阶段 | 安装程序做的事 |
| --- | --- |
| 安装 | 解包 11 个驱动文件（按架构选用 `ptimage.inf` 或 `ptimage_x86.inf`）→ `certutil -addstore Root/TrustedPublisher` 信任签名证书→ 运行 `ptimageinstall.exe install <inf> <名字> PORTPROMPT: <日志>`：`XcvData(AddPort)` 确保端口存在 → `AddPrinterDriverExW`（v4 驱动，`APD_COPY_ALL_FILES`）注册驱动 → `AddPrinterW` 建队列，每一步的 Win32 错误码都写进安装日志→ 读注册表确认队列存在（失败时 `printui /if /q` 兜底一次）→ 写“设置 → 应用”卸载项 → `WriteUninstaller`。**整个安装过程不执行任何 PowerShell 脚本** |
| 卸载 | 读驱动项 `InfPath` 记下 `oemNN.inf` → `Remove-Printer` → `Remove-PrinterDriver` → `pnputil /delete-driver oemNN.inf /uninstall /force` → `certutil -delstore` 删除 Root 与 TrustedPublisher 里的证书 → 删除卸载项/安装项注册表 → 延迟删除程序目录 |

安装失败时安装程序会弹窗并打开 `%ProgramData%\Print to image\`，其中的 `install.log` 记录了
`pnputil` / `printui` 的退出码与输出；**安装成功则不留下任何日志文件**。

驱动运行期同样只在出错时才写日志：`%ProgramData%\Print to imageilter-<pid>.log`
（无权限时退到 `%LOCALAPPDATA%\Print to image`，再退到临时目录）。日志是环形缓冲
（最近 64 条），只有 `FilterLogError` 触发时才会把上下文一次性落盘，打印成功的任务不产生文件。

## 安装 / 卸载

**安装**：双击 `print-to-images-1.0.0-setup.exe`（需要管理员），按向导完成。
静默安装：`print-to-images-1.0.0-setup.exe /S /D=C:\Program Files\Print to image`

**卸载**（三种方式，效果相同）：

1. 设置 → 应用 → 已安装的应用 → `Print to image 打印机驱动` → 卸载；
2. `"C:\Program Files\Print to image\Uninstall.exe"`；
3. 静默：`"C:\Program Files\Print to image\Uninstall.exe" /S`。

卸载会清除：打印机队列、打印机驱动、DriverStore 里的驱动包、自签名证书、
注册表项与程序目录。

## 使用

1. 任意程序里打印，选择 **Print to image**；
2. 在「打印输出另存为」对话框中点“保存类型”，选 JPEG / PNG / BMP（默认 PNG）；
3. 输入文件名并保存 —— 第 1 页写入该文件；
4. 多页文档其余页面自动生成 `名字-2.png`、`名字-3.png`…（与首张同目录）；
5. 在“打印首选项 → 纸张/质量”里可改 DPI（96/150/300/600）与纸张尺寸。

DPI 也可由程序指定：过滤器读取 PrintTicket 的 `PageResolution`，取值限制在 72–1200 之间，
取不到时用 300。

## 安装后验收

```powershell
powershell -ExecutionPolicy Bypass -File build\tests\verify-driver.ps1
```

普通权限即可。脚本会检查已安装的队列与驱动、`XpsFormat=XPS`、另存为类型只有三种，
然后用 `PrintToFile` 真正打印：1 页 → PNG/JPEG/BMP 三个非空文件（并校验文件头），
3 页 → `multi.png`、`multi-2.png`、`multi-3.png`。全部通过时输出 `VERIFY PASSED`。

## 实现要点

- **为什么是 v4 驱动**：v4 + PDC 与系统自带的 Microsoft Print to PDF 同构，安装走
  `pnputil`/`Add-PrinterDriver`，不需要 v3 的 Unidrv 运行时与 GPD 包含文件。
- **输出格式由“另存为”决定**：`ptimage-manifest.ini` 的 `[FileSave]` 列出
  `jpg=100 / png=101 / bmp=102`（各自独立资源 ID，避免同一类型出现两条），
  `ResourceFile=ptimageres.dll` 提供 `JPEG 图像 (*.jpg)` 等文案。
- **`XpsFormat=XPS` 是硬性要求**：若写 `OpenXPS`，Spooler 会把 OpenXPS 命名空间
  （`http://schemas.openxps.org/oxps/v1.0`）的页面交给过滤器，而
  `IXpsOMObjectFactory::CreatePageFromStream` 只认经典 XPS 命名空间，会返回
  `0x8052000C XPS_E_INVALID_MARKUP`，结果是 0 字节文件、多页只出一个文件。
- **渲染链路**：`IXpsDocumentProvider` 取 `IFixedPage` → `OMConvertor` 组装 `IXpsOMPage`
  → `XpsRasterService` 按 16 MB 带宽分条 `RasterizeRect` → WIC 编码 → `IPrintWriteStream`
  （第 1 页）或同名 `-N` 文件（后续页）。
- **第 1 页整体在内存里编码**：某些文件夹（如用户 Documents 下的子目录）里，过滤器
  自己创建临时文件再读回来的发布过程会被拒绝（`0x80070005`）；改为
  `CreateStreamOnHGlobal` 内存流后不再依赖对输出目录的写权限。
- **白底合成**：光栅化器返回的是预乘 alpha（`32bppPBGRA`），直接丢弃 alpha 会让透明区变黑；
  编码前按 `value + (255 * (255 - alpha) + 127) / 255` 合成到白底。

## 排错

> 安装失败时先看 `%ProgramData%\Print to image\install.log`，里面记录了驱动注册的每一步、证书信任状态，以及失败时自动附上的 `C:\Windows\INF\setupapi.dev.log` 末尾 40 行。


| 现象 | 排查 |
| --- | --- |
| 打印后没有文件 | 看「事件查看器 → 应用程序和服务日志 → Microsoft → Windows → PrintService → Admin」，事件 372 会给出 Win32 错误码：`5` 是拒绝访问，`0x8052000C` 是页面标记无法解析 |
| 安装卡在“正在创建打印机队列...” | 旧版用 `rundll32 printui.dll,PrintUIEntry /if` 建队列，该命令会弹出可能被安装窗口遮住的对话框并一直等待。1.0.0 起改为 `Add-PrinterDriver` + `Add-Printer`（有退出码、无界面）。若仍卡住：Alt+Tab 看是否有隐藏的驱动信任对话框，或手动执行 `Add-PrinterDriver -Name "Print to image"` 与 `Add-Printer -Name "Print to image" -DriverName "Print to image" -PortName "PORTPROMPT:"` |
| 安装报“无法创建打印机” | 打开 `%ProgramData%\Print to image\install.log`，其中记录 `pnputil`、`printui`、创建队列脚本的退出码与完整输出 |
| 安装日志里 `create-queue exit=1`，`pnputil` 说“已成功添加驱动程序包。(系统中已存在)”，`Add-PrinterDriver` 报“驱动程序存储区中不存在指定的驱动程序” | 上次卸载/失败安装把驱动包留在了 DriverStore，而 `pnputil /add-driver` 对已存在的包不会再安装一遍；这种残留状态下 `HKLM\SYSTEM\DriverDatabase\DriverInfFiles` 里已无对应 `oemNN.inf` 键。安装程序现在会先清理：注册表反查 → 扫描 `%SystemRoot%\INF\oem*.inf` 中内容含 `ptimage.cat` 的文件取得发布名 → `pnputil /delete-driver`，结果写在 `install.log` 的 `remove-driver exit=` 段 |
| 设备管理器/打印机里残留旧驱动 | 运行 `pnputil /enum-drivers`（找 `ptimage.inf`），或直接卸载后重装；安装程序会在安装前清理旧驱动包 |
| 卸载后证书还在 | 证书指纹每次构建可能变化，用旧安装包卸载只删除旧指纹；可在 `certmgr.msc` 手动删除 `CN=Print to image self-signed driver` |

## 许可

本项目整体以 **Microsoft Public License (MS-PL)** 发布，完整文本见 [`LICENSE`](LICENSE)。

`src\OMConvertor.*`、`src\PThandler.*`、`src\Exception.*`、`src\filtertypes.h`、
`src\UnknownBase.h`、`src\xpsrasfilter.*`、`src\rasinterface.*` 源自 Microsoft
[windows-driver-samples](https://github.com/microsoft/windows-driver-samples) 的
`print\XpsRasFilter` 示例（快照 [`ba69fa3`](https://github.com/microsoft/windows-driver-samples/tree/ba69fa3c072abdb56029a324f0e6e23eea1b33d3/print/XpsRasFilter)，2022-05-24），
版权归 Microsoft Corporation，**采用 MS-PL（不是 MIT）**，原版权头予以保留；
逐文件说明见 [`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)。

> 注意：该示例已从上游主分支删除（提交 `cbbd53ed`，2023-08-10，PR #1017 "Remove Print Driver Samples"），
> 上游现在只保留 Print Support App (PSA) 路线，因此主分支上直接浏览不到 `print/XpsRasFilter`；
> 仓库根 `LICENSE` 在删除前后都是 MS-PL，授权不受影响。

MS-PL 要求：以源码形式分发时须附带完整许可文本（随仓库的 `LICENSE`，安装程序也会把它装到
`%ProgramFiles%\Print to image\LICENSES.txt`）；分发二进制时须保留原有版权与归属声明。
本项目与 Microsoft 无隶属或背书关系，相关商标仅用于描述兼容性。

自签名证书仅用于测试部署：生产分发请改用受信任 CA 签发的代码签名证书，并重新签发 `ptimage.cat`。

; ---------------------------------------------------------------------------
; Print to image - NSIS 安装/卸载程序（安装与卸载逻辑全部内置，无外部脚本）
;
; 由 installer\build-nsis.ps1 生成：
;   makensis /DPAYLOAD=... /DOUTFILE=... /DCERTTHUMB=... installer\ptimage.nsi
;
; 安装包内容：驱动本体 10 个文件（x64 两个 DLL + x86\ 子目录两个 DLL + 6 个共享文件）
; 支持架构：x64 与 x86（各装对应位宽的过滤器与资源 DLL）
; 兼容性：理论最低 Windows 8（6.2.9000）
; 日志：仅在失败时写 %ProgramData%\Print to image\install.log，成功则不留日志
;
; 安装：信任证书 → ptimageinstall.exe 用 Win32 API 注册驱动并创建 PORTPROMPT: 队列
;       → 校验队列已注册 → 写“设置-应用”卸载项 → WriteUninstaller
; 卸载：ptimageinstall.exe 删除队列与驱动 → SetupUninstallOEMInf 删除驱动包 → certutil 删除证书
;       → 删除注册表项 → 延迟删除程序目录
; ---------------------------------------------------------------------------

Unicode true

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "x64.nsh"

!ifndef PAYLOAD
  !define PAYLOAD "..\build\nsis\payload"
!endif
!ifndef OUTFILE
  !define OUTFILE "..\dist\print-to-images-1.0.0-setup.exe"
!endif
!ifndef CERTTHUMB
  !define CERTTHUMB ""
!endif

!define APP_DISPLAY "Print to image 打印机驱动"
!define APP_VERSION "1.0.0"
!define PRINTER_NAME "Print to image"
!define PORT_NAME "PORTPROMPT:"
!define ARP_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\print-to-images"
!define APP_KEY "Software\print-to-images"
!define QUEUE_KEY "SYSTEM\CurrentControlSet\Control\Print\Printers\${PRINTER_NAME}"

Name "${APP_DISPLAY}"
Caption "${APP_DISPLAY} 安装程序"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Print to image"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
SetCompressorDictSize 32
BrandingText "${APP_DISPLAY} ${APP_VERSION}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "${APP_DISPLAY}"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileDescription" "${APP_DISPLAY} 安装程序"
VIAddVersionKey "LegalCopyright" "Print to image"

!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

!define MUI_WELCOMEPAGE_TITLE "安装 ${APP_DISPLAY}"
!define MUI_WELCOMEPAGE_TEXT "本程序将安装名为「${PRINTER_NAME}」的虚拟打印机（版本 ${APP_VERSION}）。$\r$\n$\r$\n打印时选择该打印机，输出支持 JPEG、PNG、BMP 三种图片格式，文件由系统「打印输出另存为」对话框指定。$\r$\n$\r$\n多页文档会保存为多张图片：第一页使用您选择的文件名，后续页面自动追加 -2、-3 等后缀。$\r$\n$\r$\n同时提供 64 位与 32 位驱动，安装程序会自动选择与当前系统匹配的版本。$\r$\n$\r$\n兼容性：Windows 8 (6.2.9000) 及以上。$\r$\n$\r$\n单击「下一步」继续。"

!define MUI_FINISHPAGE_TITLE "安装完成"
!define MUI_FINISHPAGE_TEXT "打印机「${PRINTER_NAME}」已创建。$\r$\n$\r$\n现在可以在任意程序中打印，并在「打印输出另存为」对话框中选择 JPEG / PNG / BMP 格式。$\r$\n$\r$\n如需卸载：设置 → 应用 → 已安装的应用 → ${APP_DISPLAY} → 卸载。"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSES.txt"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"

; ---------------------------------------------------------------------------
; 初始化：确定架构（$R6 为驱动注册表环境名）、安装目录、系统版本
; ---------------------------------------------------------------------------
Function .onInit
  ${If} ${RunningX64}
    SetRegView 64
    StrCpy $R6 "Windows x64"
    StrCpy $INSTDIR "$PROGRAMFILES64\Print to image"
  ${Else}
    SetRegView 32
    StrCpy $R6 "Windows NT x86"
    StrCpy $INSTDIR "$PROGRAMFILES\Print to image"
  ${EndIf}

  ; 把版本折算成一个整数：Major*1000000 + Minor*10000 + Build
  ; 6.2.9000 → 6029000，10.0.19041 → 1019041（与 6.2.9000 比较即可）
  ${DisableX64FSRedirection}

  ; 驱动按架构使用各自的 INF：两个都是“纯” NTamd64 / NTx86，不带版本修饰，
  ; 因为带修饰的多架构 INF 在部分 Windows 10 上能暂存却无法注册。
  ${If} ${RunningX64}
    StrCpy $R6 "Windows x64"
    StrCpy $R8 "ptimage.inf"
  ${Else}
    StrCpy $R6 "Windows NT x86"
    StrCpy $R8 "ptimage_x86.inf"
  ${EndIf}
  ; 版本下限检查：直接读注册表，不调用 PowerShell
  ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentMajorVersionNumber"
  ReadRegStr $1 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentMinorVersionNumber"
  ReadRegStr $2 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentBuildNumber"
  ${If} $0 == ""
    ; Windows 8 / 8.1 没有 CurrentMajorVersionNumber，只有 "6.2" / "6.3" 形式的 CurrentVersion
    ReadRegStr $0 HKLM "SOFTWARE\Microsoft\Windows NT\CurrentVersion" "CurrentVersion"
    StrCpy $1 $0 1 2
    StrCpy $0 $0 1
  ${EndIf}
  ${If} $2 == ""
    StrCpy $2 0
  ${EndIf}
  IntOp $0 $0 * 1000000
  IntOp $1 $1 * 10000
  IntOp $0 $0 + $1
  IntOp $0 $0 + $2
  ${EnableX64FSRedirection}

  ${If} $0 < 6029000
    MessageBox MB_ICONSTOP "当前系统版本过低。$\r$\n$\r$\n本程序要求 Windows 8 (6.2.9000) 或更高版本，建议 Windows 10 Build 19041 及以上。" /SD IDOK
    Abort
  ${EndIf}

  ReadRegStr $2 HKLM "${APP_KEY}" "InstallDir"
  ${If} $2 != ""
    StrCpy $INSTDIR $2
  ${EndIf}
FunctionEnd

; ---------------------------------------------------------------------------
; 删除驱动包（DriverStore）——宏，安装与卸载共用
;   输入：$R9 = Version-4 注册表里的 InfPath
;   输出：$R3 = 0 已删除 / 3 没有可删的包 / 其它 = pnputil 返回码
;   说明：InfPath 指向 FileRepository\ptimage.inf_amd64_<hash>\ptimage.inf，
;         而 pnputil /delete-driver 要的是发布名 oemNN.inf；直接把文件名传进去
;         会报“指定的文件不是安装的 OEM INF”，驱动包就残留在 DriverStore 里。
;         这里按 FileRepository 目录名在 DriverDatabase\DriverInfFiles 中反查
;         发布名（该键下 oemNN.inf 的默认值正是 <inf>_<arch>_<hash> 目录名）。
; ---------------------------------------------------------------------------
!macro DRIVER_REMOVAL_BODY
  nsExec::ExecToStack '"$R1" remove-package "$R4"'
  Pop $R3
  Pop $5

  ${If} $R3 == 0
    DetailPrint "已清理 DriverStore 中的 ptimage.inf 驱动包（如存在）。"
  ${Else}
    DetailPrint "清理驱动包返回 $R3。"
  ${EndIf}
!macroend

; 安装阶段（升级时清理旧版本）使用
Function RemoveDriverPackage
  !insertmacro DRIVER_REMOVAL_BODY
FunctionEnd

; 卸载阶段必须使用 un. 前缀的函数
Function un.RemoveDriverPackage
  !insertmacro DRIVER_REMOVAL_BODY
FunctionEnd

; ---------------------------------------------------------------------------
; 清理旧版本：删除队列与驱动包
;   输入：$R6 = 驱动环境名
; ---------------------------------------------------------------------------
Function RemovePreviousInstall
  DetailPrint "删除旧的打印机队列与驱动..."
  nsExec::ExecToStack '"$R1" uninstall "${PRINTER_NAME}" "$R4"'
  Pop $3
  Pop $5
  Call RemoveDriverPackage
FunctionEnd

; ---------------------------------------------------------------------------
; 安装
; ---------------------------------------------------------------------------
Section "安装驱动和打印机" SEC_MAIN
  ${If} ${RunningX64}
    SetRegView 64
  ${Else}
    SetRegView 32
  ${EndIf}
  SetShellVarContext all

  ; NSIS 本体是 32 位程序：关闭 WOW64 重定向，$SYSDIR 才是真正的 System32
  ${DisableX64FSRedirection}

  ; 安装日志只在失败时保留，先写到临时目录，成功就删除
  StrCpy $R4 "$TEMP\ptimage-install.log"
  Delete "$R4"
  FileOpen $2 "$R4" w
  FileWrite $2 "==== Print to image ${APP_VERSION} install log ====$\r$\n"
  FileClose $2

  SetOutPath "$INSTDIR"
  File "${PAYLOAD}\ptimagefilter.dll"
  File "${PAYLOAD}\ptimageres.dll"
  File "${PAYLOAD}\ptimage.inf"
  File "${PAYLOAD}\ptimage_x86.inf"
  File "${PAYLOAD}\ptimage.cat"
  File "${PAYLOAD}\ptimage.cer"
  File "${PAYLOAD}\ptimage-manifest.ini"
  File "${PAYLOAD}\ptimage-pdc.xml"
  File "${PAYLOAD}\ptimage-pipelineconfig.xml"
  File "LICENSES.txt"
  File "${PAYLOAD}\ptimageinstall.exe"
  File "${PAYLOAD}\ptimageinstall32.exe"

  ${If} ${RunningX64}
    StrCpy $R1 "$INSTDIR\ptimageinstall.exe"
  ${Else}
    StrCpy $R1 "$INSTDIR\ptimageinstall32.exe"
  ${EndIf}

  Call RemovePreviousInstall

  SetOutPath "$INSTDIR\x86"
  File "${PAYLOAD}\x86\ptimagefilter.dll"
  File "${PAYLOAD}\x86\ptimageres.dll"
  SetOutPath "$INSTDIR"

  DetailPrint "正在信任驱动签名证书..."
  nsExec::ExecToLog '"$SYSDIR\certutil.exe" -addstore -f Root "$INSTDIR\ptimage.cer"'
  nsExec::ExecToLog '"$SYSDIR\certutil.exe" -addstore -f TrustedPublisher "$INSTDIR\ptimage.cer"'

  DetailPrint "正在安装驱动并创建打印机队列（可能需要 10-30 秒）..."
  Call InstallDriver
  ${If} $R0 == 0
    DetailPrint "驱动/队列未出现，1.5 秒后重试一次..."
    Sleep 1500
    Call InstallDriver
  ${EndIf}

  ${If} $R0 == 0
    DetailPrint "脚本方式失败，改用 printui 兜底..."
    nsExec::ExecToLog '"$SYSDIR\rundll32.exe" printui.dll,PrintUIEntry /if /b "${PRINTER_NAME}" /f "$INSTDIR\$R8" /r "${PORT_NAME}" /m "${PRINTER_NAME}" /q'
    Sleep 2000
    Call CheckQueue
  ${EndIf}

  ${If} $R0 == 0
    CreateDirectory "$APPDATA\Print to image"
    CopyFiles /SILENT "$R4" "$APPDATA\Print to image\install.log"
    MessageBox MB_ICONSTOP|MB_OK "无法创建打印机“${PRINTER_NAME}”，安装已中止。$\r$\n$\r$\n日志：$APPDATA\Print to image\install.log$\r$\n$\r$\n接下来会打开该文件夹。"
    Exec 'explorer.exe "$APPDATA\Print to image"'
    Quit
  ${EndIf}

  ; 成功：不保留安装日志
  Delete "$R4"

  DetailPrint "正在注册卸载程序..."
  WriteRegStr HKLM "${ARP_KEY}" "DisplayName" "${APP_DISPLAY}"
  WriteRegStr HKLM "${ARP_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "${ARP_KEY}" "Publisher" "Print to image"
  WriteRegStr HKLM "${ARP_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${ARP_KEY}" "DisplayIcon" "$INSTDIR\ptimagefilter.dll"
  WriteRegStr HKLM "${ARP_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${ARP_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegStr HKLM "${ARP_KEY}" "Comments" "把打印输出保存为 JPEG / PNG / BMP 图片"
  WriteRegDWORD HKLM "${ARP_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${ARP_KEY}" "NoRepair" 1

  ${GetTime} "" "L" $0 $1 $2 $3 $4 $5 $6
  WriteRegStr HKLM "${ARP_KEY}" "InstallDate" "$0$1$2"

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${ARP_KEY}" "EstimatedSize" "$0"

  WriteRegStr HKLM "${APP_KEY}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${APP_KEY}" "Version" "${APP_VERSION}"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  DetailPrint "安装结束。"

  ${EnableX64FSRedirection}
SectionEnd

; ---------------------------------------------------------------------------
; 卸载
; ---------------------------------------------------------------------------
Section "Uninstall"
  ${If} ${RunningX64}
    SetRegView 64
    StrCpy $R6 "Windows x64"
  ${Else}
    SetRegView 32
    StrCpy $R6 "Windows NT x86"
  ${EndIf}
  SetShellVarContext all

  ${DisableX64FSRedirection}

  !insertmacro MUI_HEADER_TEXT "正在卸载 ${APP_DISPLAY}" "正在删除打印机、驱动、证书与程序文件..."

  ${If} ${RunningX64}
    StrCpy $R1 "$INSTDIR\ptimageinstall.exe"
  ${Else}
    StrCpy $R1 "$INSTDIR\ptimageinstall32.exe"
  ${EndIf}
  StrCpy $R4 "$TEMP\ptimage-uninstall.log"

  DetailPrint "正在删除打印机队列与驱动..."
  nsExec::ExecToStack '"$R1" uninstall "${PRINTER_NAME}" "$R4"'
  Pop $3
  Pop $5

  DetailPrint "正在删除打印机驱动与驱动包..."
  Call un.RemoveDriverPackage

  ${If} "${CERTTHUMB}" != ""
    DetailPrint "正在删除自签名证书..."
    nsExec::ExecToLog '"$SYSDIR\certutil.exe" -delstore Root "${CERTTHUMB}"'
    nsExec::ExecToLog '"$SYSDIR\certutil.exe" -delstore TrustedPublisher "${CERTTHUMB}"'
  ${EndIf}

  DetailPrint "正在删除注册表项..."
  DeleteRegKey HKLM "${ARP_KEY}"
  DeleteRegKey HKLM "${APP_KEY}"

  DetailPrint "正在删除程序文件..."
  SetOutPath "$TEMP"
  Exec 'cmd /c ping -n 3 127.0.0.1 >nul & rd /s /q "$INSTDIR"'

  ${EnableX64FSRedirection}
SectionEnd

; ---------------------------------------------------------------------------
; 检查队列是否已注册：$R0 = 1 存在，0 不存在
;   以注册表为准（不依赖 PowerShell 引号转义）
; ---------------------------------------------------------------------------
; 注册驱动并创建队列：由 ptimageinstall.exe 调用 AddPrinterDriverExW / AddPrinterW 完成，
; 不使用任何 PowerShell 脚本；printui /if 只作为最后兜底。
Function InstallDriver
  DetailPrint "注册打印驱动（$R8）..."
  nsExec::ExecToStack '"$R1" install "$INSTDIR\$R8" "${PRINTER_NAME}" "${PORT_NAME}" "$R4"'
  Pop $3
  Pop $5
  DetailPrint "注册驱动返回 $3"
  Call CheckQueue
FunctionEnd

Function CheckQueue
  StrCpy $R0 0
  StrCpy $R7 0
  ${Do}
    ReadRegStr $R5 HKLM "${QUEUE_KEY}" "Port"
    ${If} $R5 != ""
      StrCpy $R0 1
      ${ExitDo}
    ${EndIf}
    IntOp $R7 $R7 + 1
    ${If} $R7 < 6
      Sleep 700
    ${EndIf}
  ${LoopUntil} $R7 >= 6
FunctionEnd

@echo off
rem ---------------------------------------------------------------------------
rem  Print to image - build the render filter and the resource DLL
rem
rem  Builds both architectures:
rem    build\ptimagefilter.dll, build\ptimageres.dll          (x64)
rem    build\x86\ptimagefilter.dll, build\x86\ptimageres.dll  (x86)
rem ---------------------------------------------------------------------------
setlocal

set VCDIR=E:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build

if not exist "%VCDIR%\vcvars64.bat" (
    echo ERROR: vcvars64.bat not found under "%VCDIR%"
    exit /b 1
)
if not exist "%VCDIR%\vcvars32.bat" (
    echo ERROR: vcvars32.bat not found under "%VCDIR%"
    exit /b 1
)

pushd "%~dp0.."

set SOURCES=src\Exception.cpp src\OMConvertor.cpp src\PThandler.cpp src\rasinterface.cpp src\xpsrasfilter.cpp src\dllentry.cpp

set LIBS=ole32.lib oleaut32.lib uuid.lib user32.lib gdi32.lib advapi32.lib windowscodecs.lib shlwapi.lib msxml6.lib prntvpt.lib winspool.lib

rem ---------------------------------------------------------------- x64 ------
call "%VCDIR%\vcvars64.bat" >nul
if errorlevel 1 (
    echo ERROR: failed to initialise the x64 Visual C++ environment.
    goto :failed
)

if not exist build\obj mkdir build\obj

set COMMON=/nologo /c /EHsc /W3 /O2 /MT /GS- /DUNICODE /D_UNICODE /DWIN32 /DWIN64 /D_AMD64_ /D_USRDLL /Fo:build\obj\ /I src

for %%F in (%SOURCES%) do (
    cl %COMMON% %%F || goto :failed
)

link /nologo /DLL /MACHINE:X64 ^
     /DEF:src\ptimagefilter.def ^
     /OUT:build\ptimagefilter.dll ^
     /IMPLIB:build\ptimagefilter.lib ^
     /PDB:build\ptimagefilter.pdb ^
     /MANIFEST:NO /DYNAMICBASE /NXCOMPAT ^
     build\obj\Exception.obj build\obj\OMConvertor.obj build\obj\PThandler.obj ^
     build\obj\rasinterface.obj build\obj\xpsrasfilter.obj build\obj\dllentry.obj ^
     %LIBS% || goto :failed

rem --- resource-only DLL: strings used by the "Save Print Output As" dialog ---
rc /nologo /fo build\obj\ptimageres.res src\ptimageres.rc || goto :failed

link /nologo /DLL /NOENTRY /MACHINE:X64 ^
     /OUT:build\ptimageres.dll /MANIFEST:NO ^
     build\obj\ptimageres.res || goto :failed

echo BUILD OK: build\ptimagefilter.dll
echo BUILD OK: build\ptimageres.dll

rem --- 安装辅助程序：用 Win32 API 注册 v4 驱动并建队列（不使用 PowerShell） ---
rem --- installer helper: registers the v4 driver and queue through Win32 only ---
cl /nologo /c /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DWIN32 /DWIN64 /D_AMD64_ ^
   /Fo:build\obj\ /I src tools\ptimageinstall\ptimageinstall.cpp || goto :failed

link /nologo /SUBSYSTEM:CONSOLE /MACHINE:X64 /OUT:build\ptimageinstall.exe ^
     /MANIFEST:NO /DYNAMICBASE /NXCOMPAT ^
     build\obj\ptimageinstall.obj winspool.lib advapi32.lib user32.lib setupapi.lib || goto :failed

echo BUILD OK: build\ptimageinstall.exe

rem ---------------------------------------------------------------- x86 ------
call "%VCDIR%\vcvars32.bat" >nul
if errorlevel 1 (
    echo ERROR: failed to initialise the x86 Visual C++ environment.
    goto :failed
)

if not exist build\obj32 mkdir build\obj32
if not exist build\x86 mkdir build\x86

set COMMON32=/nologo /c /EHsc /W3 /O2 /MT /GS- /DUNICODE /D_UNICODE /DWIN32 /D_X86_ /D_USRDLL /Fo:build\obj32\ /I src

for %%F in (%SOURCES%) do (
    cl %COMMON32% %%F || goto :failed
)

link /nologo /DLL /MACHINE:X86 ^
     /DEF:src\ptimagefilter.def ^
     /OUT:build\x86\ptimagefilter.dll ^
     /IMPLIB:build\x86\ptimagefilter.lib ^
     /PDB:build\x86\ptimagefilter.pdb ^
     /MANIFEST:NO /DYNAMICBASE /NXCOMPAT ^
     build\obj32\Exception.obj build\obj32\OMConvertor.obj build\obj32\PThandler.obj ^
     build\obj32\rasinterface.obj build\obj32\xpsrasfilter.obj build\obj32\dllentry.obj ^
     %LIBS% || goto :failed

rc /nologo /fo build\obj32\ptimageres.res src\ptimageres.rc || goto :failed

link /nologo /DLL /NOENTRY /MACHINE:X86 ^
     /OUT:build\x86\ptimageres.dll /MANIFEST:NO ^
     build\obj32\ptimageres.res || goto :failed

echo BUILD OK: build\x86\ptimagefilter.dll
echo BUILD OK: build\x86\ptimageres.dll

rem --- 32 位安装辅助程序 ---
cl /nologo /c /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DWIN32 /D_X86_ ^
   /Fo:build\obj32\ /I src tools\ptimageinstall\ptimageinstall.cpp || goto :failed

link /nologo /SUBSYSTEM:CONSOLE /MACHINE:X86 /OUT:build\x86\ptimageinstall.exe ^
     /MANIFEST:NO /DYNAMICBASE /NXCOMPAT ^
     build\obj32\ptimageinstall.obj winspool.lib advapi32.lib user32.lib setupapi.lib || goto :failed

echo BUILD OK: build\x86\ptimageinstall.exe

popd
exit /b 0

:failed
popd
echo.
echo BUILD FAILED
exit /b 1

@echo off
rem ---------------------------------------------------------------------------
rem  print to images - build and run the offline ImageWriter test
rem ---------------------------------------------------------------------------
setlocal

set VCVARS=E:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat

call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: failed to initialise the Visual C++ environment.
    exit /b 1
)

pushd "%~dp0.."

if not exist build\obj mkdir build\obj

set COMMON=/nologo /c /EHsc /W3 /O2 /MT /DUNICODE /D_UNICODE /DWIN32 /DWIN64 /D_AMD64_ /Fo:build\obj\ /I src

cl %COMMON% src\Exception.cpp                        || goto :failed
cl %COMMON% src\rasinterface.cpp                     || goto :failed
cl %COMMON% build\tests\testwriter.cpp               || goto :failed

link /nologo /MACHINE:X64 ^
     /OUT:build\testwriter.exe ^
     /PDB:build\testwriter.pdb ^
     build\obj\Exception.obj ^
     build\obj\rasinterface.obj ^
     build\obj\testwriter.obj ^
     ole32.lib oleaut32.lib uuid.lib user32.lib advapi32.lib ^
     windowscodecs.lib shlwapi.lib || goto :failed

echo.
echo RUNNING TEST...
build\testwriter.exe
set TESTRESULT=%ERRORLEVEL%

popd
exit /b %TESTRESULT%

:failed
popd
echo BUILD FAILED
exit /b 1

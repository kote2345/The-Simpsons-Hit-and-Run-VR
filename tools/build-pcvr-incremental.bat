@echo off
setlocal
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SRR2_VSROOT=%%I"
if not defined SRR2_VSROOT exit /b 1
call "%SRR2_VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if /i "%~1"=="--clean" (
    cmake --build build\pcvr --config RelWithDebInfo --clean-first
) else (
    cmake --build build\pcvr --config RelWithDebInfo --parallel
)
set "SRR2_BUILD_RESULT=%ERRORLEVEL%"
popd
exit /b %SRR2_BUILD_RESULT%

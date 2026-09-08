@echo off
setlocal
if "%~1"=="" exit /b 1
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SRR2_VSROOT=%%I"
if not defined SRR2_VSROOT exit /b 1
call "%SRR2_VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
pushd "%~dp0.."
"%~1\Bin\glslangValidator.exe" -V tools\test-pbr-brdf.comp -o build\pcvr\test-pbr-brdf.spv
if errorlevel 1 exit /b 1
"%~1\Bin\spirv-val.exe" --target-env vulkan1.0 build\pcvr\test-pbr-brdf.spv
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /I"%~1\Include" tools\test-pbr-brdf.cpp /Fo:build\pcvr\test-pbr-brdf.obj /Fe:build\pcvr\test-pbr-brdf.exe /link /LIBPATH:"%~1\Lib" vulkan-1.lib
if errorlevel 1 exit /b 1
build\pcvr\test-pbr-brdf.exe
set "SRR2_TEST_RESULT=%ERRORLEVEL%"
popd
exit /b %SRR2_TEST_RESULT%

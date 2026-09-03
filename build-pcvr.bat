@echo off
setlocal EnableExtensions

pushd "%~dp0"

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    popd
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if not exist "%VSWHERE%" (
        echo ERROR: Visual Studio 2022 Build Tools were not found.
        popd
        exit /b 1
    )
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
    if not defined VSROOT (
        echo ERROR: Visual Studio C++ x64 tools are not installed.
        popd
        exit /b 1
    )
    call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

if "%VCPKG_ROOT%"=="" (
    if exist "%~dp0vcpkg\scripts\buildsystems\vcpkg.cmake" (
        set "VCPKG_ROOT=%~dp0vcpkg"
    ) else (
        echo ERROR: VCPKG_ROOT is not set.
        echo Install vcpkg or clone it into "%~dp0vcpkg".
        popd
        exit /b 1
    )
)

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo ERROR: Invalid VCPKG_ROOT: "%VCPKG_ROOT%"
    popd
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 (
        popd
        exit /b 1
    )
)

"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows --x-manifest-root="%~dp0"
if errorlevel 1 (
    popd
    exit /b 1
)

cmake -S . -B build\pcvr -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DSRR2_ENABLE_OPENXR=ON ^
    -DSRR2_VR_RENDERER=VULKAN ^
    -DSRR2_BUILD_TESTS=OFF ^
    -DSRR2_USE_PCH=OFF
if errorlevel 1 (
    popd
    exit /b 1
)

cmake --build build\pcvr --config RelWithDebInfo
if errorlevel 1 (
    popd
    exit /b 1
)

echo.
echo PCVR build completed successfully.
echo Output: "%~dp0build\pcvr\code\SRR2.exe"
popd

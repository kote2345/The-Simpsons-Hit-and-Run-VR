@echo off
setlocal

if "%VCPKG_ROOT%"=="" (
    echo VCPKG_ROOT is not set. Install vcpkg and point VCPKG_ROOT to it.
    exit /b 1
)

cmake -S . -B build\pcvr -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DSRR2_ENABLE_OPENXR=ON ^
    -DSRR2_VR_RENDERER=VULKAN ^
    -DSRR2_BUILD_TESTS=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build build\pcvr

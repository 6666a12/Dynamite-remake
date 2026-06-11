@echo off
REM Dynamite CI Test Runner (Windows)
REM Usage: ci_test.bat
setlocal
set BUILD_DIR=%~dp0core\build

echo ========================================
echo Dynamite CI Test Suite
echo ========================================

echo.
echo [1/3] Configuring CMake...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_DESKTOP_EXE=OFF
if errorlevel 1 goto :error

echo.
echo [2/3] Building tests...
ninja test_core_catch2
if errorlevel 1 goto :error

echo.
echo [3/3] Running tests...
test_core_catch2.exe
set EXIT_CODE=%errorlevel%

echo.
echo ========================================
if %EXIT_CODE% equ 0 (
    echo ALL TESTS PASSED
) else (
    echo SOME TESTS FAILED (exit: %EXIT_CODE%)
)
echo ========================================
exit /b %EXIT_CODE%

:error
echo BUILD FAILED
exit /b 1

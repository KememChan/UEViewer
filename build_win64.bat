@echo off
setlocal enabledelayedexpansion

echo ========================================================
echo   UEViewer (UModel) Automated 64-bit Build Script
echo ========================================================
echo.

cd /d "%~dp0"

:: 1. Ensure Gildor's BuildTools is present
set BUILDTOOLS_DIR=C:\BuildTools
if not exist "%BUILDTOOLS_DIR%\bin\vc32tools" (
    echo [1/3] BuildTools not found at %BUILDTOOLS_DIR%.
    echo Cloning BuildTools from https://github.com/gildor2/BuildTools.git ...
    git clone https://github.com/gildor2/BuildTools.git %BUILDTOOLS_DIR%
    if errorlevel 1 (
        echo [ERROR] Failed to clone BuildTools. Please check your internet connection or git.
        pause
        exit /b 1
    )
) else (
    echo [1/3] Found BuildTools at %BUILDTOOLS_DIR%.
)

:: 2. Check for Visual Studio C++ Compiler
echo [2/3] Checking for Visual Studio C++ compiler...
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set HAS_VS=0
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VS_PATH=%%i
    )
    if defined VS_PATH set HAS_VS=1
)

if "%HAS_VS%"=="0" (
    echo.
    echo [NOTICE] Visual Studio C++ compiler is not detected on this machine.
    echo UEViewer requires MSVC compiler to compile on Windows.
    echo.
    echo You can install it automatically by running in an Administrator PowerShell:
    echo   winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
    echo.
    echo Alternatively, push this repository to GitHub and GitHub Actions will
    echo compile it automatically for you without installing anything locally!
    echo.
    pause
    exit /b 1
)

:: 3. Run build.sh with BuildTools in PATH
echo [3/3] Compiling umodel_64.exe...
set PATH=%BUILDTOOLS_DIR%\bin;%PATH%
bash build.sh --64

if exist "dist\umodel_64.exe" (
    echo.
    echo ========================================================
    echo   BUILD SUCCESSFUL!
    echo   Shippable package is ready in .\dist\ folder:
    echo     - dist\umodel_64.exe
    echo     - dist\SDL2_64.dll
    echo     - dist\oo2core_9_win64.dll
    echo     - dist\keys.json
    echo ========================================================
) else if exist "umodel_64.exe" (
    echo.
    echo ========================================================
    echo   BUILD SUCCESSFUL: umodel_64.exe is ready!
    echo ========================================================
) else (
    echo.
    echo [ERROR] Build failed. Review the messages above.
)

pause

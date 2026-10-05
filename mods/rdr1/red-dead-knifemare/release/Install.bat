@echo off
setlocal
cd /d "%~dp0"

echo Red Dead Knifemare Installer
echo ============================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RedDeadKnifemareInstaller\install.ps1"
set "EXITCODE=%ERRORLEVEL%"

echo.
if "%EXITCODE%"=="0" (
    echo Installation completed successfully.
) else (
    echo Installation failed with exit code %EXITCODE%.
    echo Read the message above before making any manual file changes.
)
echo.
pause
exit /b %EXITCODE%

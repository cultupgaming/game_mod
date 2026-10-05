@echo off
setlocal
cd /d "%~dp0"

echo Red Dead Knifemare Uninstaller
echo ==============================
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RedDeadKnifemareInstaller\uninstall.ps1"
set "EXITCODE=%ERRORLEVEL%"

echo.
if "%EXITCODE%"=="0" (
    echo Uninstallation completed successfully.
) else (
    echo Uninstallation stopped with exit code %EXITCODE%.
    echo Read the message above. The uninstaller refuses unsafe restores.
)
echo.
pause
exit /b %EXITCODE%

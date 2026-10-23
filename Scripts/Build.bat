@echo off
rem Builds the plugin on its own with the engine's BuildPlugin command (no game project needed).
rem Output: Packaged\AssetGraphExporter   Log: Logs\build.log
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if exist "%~dp0LocalConfig.bat" call "%~dp0LocalConfig.bat"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

if not exist "%ROOT%\Logs" mkdir "%ROOT%\Logs"
set "LOG=%ROOT%\Logs\build.log"

echo Building AssetGraphExporter with "%UE_ROOT%"
echo This takes a few minutes. Full log: %LOG%
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="%ROOT%\AssetGraphExporter\AssetGraphExporter.uplugin" -Package="%ROOT%\Packaged\AssetGraphExporter" -NoTargetPlatforms > "%LOG%" 2>&1
set "RESULT=%ERRORLEVEL%"

echo.
echo ---- last lines of the log ----
powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%LOG%'"
echo.
if "%RESULT%"=="0" (
	echo BUILD SUCCEEDED
) else (
	echo BUILD FAILED with exit code %RESULT%. See Logs\build.log
)
exit /b %RESULT%

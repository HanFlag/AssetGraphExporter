@echo off
rem Builds the plugins on their own with the engine's BuildPlugin command (no game project needed).
rem   Build.bat        both plugins
rem   Build.bat core   AssetGraphExporter only
rem   Build.bat mcp    AssetGraphExporterMcp only (needs the Unreal MCP engine plugin)
rem Output: Packaged\<Plugin>   Logs: Logs\build.log, Logs\build_mcp.log
rem Stop Serve.bat first (age_shutdown or Ctrl+C): a running server keeps Packaged\ DLLs locked.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if exist "%~dp0LocalConfig.bat" call "%~dp0LocalConfig.bat"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
if not exist "%ROOT%\Logs" mkdir "%ROOT%\Logs"

set "WHAT=%~1"
if "%WHAT%"=="" set "WHAT=all"
if /i not "%WHAT%"=="all" if /i not "%WHAT%"=="core" if /i not "%WHAT%"=="mcp" (
	echo Usage: Build.bat [all^|core^|mcp]
	exit /b 1
)

set "CORE=%ROOT%\AssetGraphExporter\AssetGraphExporter.uplugin"
set "MCP=%ROOT%\AssetGraphExporterMcp\AssetGraphExporterMcp.uplugin"
set "RUNUAT=%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
echo Engine: "%UE_ROOT%". Each plugin takes a few minutes.
if /i "%WHAT%"=="mcp" goto :mcp

echo Building AssetGraphExporter ...
call "%RUNUAT%" BuildPlugin -Plugin="%CORE%" -Package="%ROOT%\Packaged\AssetGraphExporter" -NoTargetPlatforms > "%ROOT%\Logs\build.log" 2>&1
set "RESULT=%ERRORLEVEL%"
call :ShowLog "%ROOT%\Logs\build.log" AssetGraphExporter %RESULT%
if not "%RESULT%"=="0" exit /b %RESULT%
if /i "%WHAT%"=="core" exit /b 0

:mcp
rem -Dependencies puts the core plugin next to this one in the temporary host project, so it can link against it.
echo Building AssetGraphExporterMcp ...
call "%RUNUAT%" BuildPlugin -Plugin="%MCP%" -Package="%ROOT%\Packaged\AssetGraphExporterMcp" -Dependencies="%CORE%" -NoTargetPlatforms > "%ROOT%\Logs\build_mcp.log" 2>&1
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" (
	rem The activity window, with the C# compiler that ships with Windows, so building it needs nothing installed.
	echo Building the activity window ... >> "%ROOT%\Logs\build_mcp.log"
	"%SystemRoot%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:winexe /optimize+ /warnaserror+ /codepage:65001 /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /out:"%ROOT%\Packaged\AssetGraphExporterMcp\Binaries\Win64\AGEActivityWindow.exe" "%ROOT%\AssetGraphExporterMcp\Source\ActivityWindow\AGEActivityWindow.cs" >> "%ROOT%\Logs\build_mcp.log" 2>&1
	if errorlevel 1 set "RESULT=1"
)
call :ShowLog "%ROOT%\Logs\build_mcp.log" AssetGraphExporterMcp %RESULT%
exit /b %RESULT%

:ShowLog
echo.
echo ---- last lines of %~1 ----
powershell -NoProfile -Command "Get-Content -Tail 30 -LiteralPath '%~1'"
echo.
if "%~3"=="0" (
	echo %~2: BUILD SUCCEEDED
) else (
	echo %~2: BUILD FAILED with exit code %~3. See %~1
)
exit /b 0

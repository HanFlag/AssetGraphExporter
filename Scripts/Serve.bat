@echo off
rem Starts the background MCP server (AssetGraphServe commandlet) for AGE_PROJECT, so Claude Code can use the age_* tools
rem (and the built-in Unreal MCP tools) while the editor is closed. Same address as the editor's server, by default
rem http://127.0.0.1:8000/mcp. Both plugins are loaded from Packaged\ with -PLUGIN= (the project is not modified).
rem It stops on the age_shutdown tool, Ctrl+C, when an Unreal Editor starts, or after 30 minutes without age_* requests.
rem Extra arguments go to the commandlet, e.g. Serve.bat -IdleMinutes=0
rem Stop it before building C++ code: while it runs, the project's and the plugins' DLLs stay loaded.
rem Startup: about a minute the first time after a reboot (the Zen storage server and shader setup start too), less afterwards.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if exist "%~dp0LocalConfig.bat" call "%~dp0LocalConfig.bat"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

if not defined AGE_PROJECT (
	echo AGE_PROJECT is not set. Copy Scripts\LocalConfig.example.bat to Scripts\LocalConfig.bat and edit it.
	exit /b 1
)
set "CORE=%ROOT%\Packaged\AssetGraphExporter\AssetGraphExporter.uplugin"
set "MCP=%ROOT%\Packaged\AssetGraphExporterMcp\AssetGraphExporterMcp.uplugin"
if not exist "%CORE%" goto :notbuilt
if not exist "%MCP%" goto :notbuilt

rem An editor serves the same port. Refuse now instead of after engine startup (the commandlet checks again later).
echo %* | find /I "-IgnoreEditor" > nul
if errorlevel 1 (
	tasklist /FI "IMAGENAME eq UnrealEditor.exe" /NH 2> nul | find /I "UnrealEditor.exe" > nul
	if not errorlevel 1 (
		echo An Unreal Editor is running. Use the MCP server inside the editor instead; both would use the same port.
		echo Pass -IgnoreEditor to start anyway.
		exit /b 1
	)
)

if not exist "%ROOT%\Logs" mkdir "%ROOT%\Logs"
set "LOG=%ROOT%\Logs\serve.log"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%AGE_PROJECT%" -run=AssetGraphServe -PLUGIN="%CORE%" -PLUGIN="%MCP%" %* -unattended -nosplash -nullrhi -nop4 -ABSLOG="%LOG%"
set "RESULT=%ERRORLEVEL%"

echo.
echo Server stopped. Exit code %RESULT%. Log: %LOG%
exit /b %RESULT%

:notbuilt
echo Plugins are not built yet. Run Scripts\Build.bat first.
exit /b 1

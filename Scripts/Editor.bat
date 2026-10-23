@echo off
rem Opens the editor for AGE_PROJECT with both plugins loaded from Packaged\ (-PLUGIN=), without installing them into the
rem project. The age_* tools are then served by the editor's Unreal MCP server (if it does not start by itself, enable
rem "Auto Start Server" in Project Settings > Plugins > Model Context Protocol, or run ModelContextProtocol.StartServer).
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

start "" "%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%AGE_PROJECT%" -PLUGIN="%CORE%" -PLUGIN="%MCP%" %*
exit /b 0

:notbuilt
echo Plugins are not built yet. Run Scripts\Build.bat first.
exit /b 1

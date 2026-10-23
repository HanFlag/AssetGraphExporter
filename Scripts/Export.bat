@echo off
rem Runs the AssetGraphExport commandlet against a project, loading the plugin from Packaged\ with -PLUGIN=
rem (the project itself is not modified). All arguments are passed to the commandlet unchanged, e.g.
rem   Export.bat -Assets=/Game/Folder/BP_Thing,/Game/Folder/ST_Tree
rem   Export.bat -Path=/Game/Folder -Class=Blueprint
rem Close the editor for that project first.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
if exist "%~dp0LocalConfig.bat" call "%~dp0LocalConfig.bat"
if not defined UE_ROOT set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

if not defined AGE_PROJECT (
	echo AGE_PROJECT is not set. Copy Scripts\LocalConfig.example.bat to Scripts\LocalConfig.bat and edit it.
	exit /b 1
)
set "PLUGIN=%ROOT%\Packaged\AssetGraphExporter\AssetGraphExporter.uplugin"
if not exist "%PLUGIN%" (
	echo Plugin is not built yet. Run Scripts\Build.bat first.
	exit /b 1
)

if not exist "%ROOT%\Logs" mkdir "%ROOT%\Logs"
set "LOG=%ROOT%\Logs\export.log"

"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%AGE_PROJECT%" -run=AssetGraphExport -PLUGIN="%PLUGIN%" %* -unattended -nosplash -nullrhi -nop4 -ABSLOG="%LOG%"
set "RESULT=%ERRORLEVEL%"

echo.
echo Exit code %RESULT%. Log: %LOG%
exit /b %RESULT%

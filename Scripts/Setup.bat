@echo off
rem One-time setup: creates the git repository for this folder and the local config file.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
cd /d "%ROOT%"

if exist ".git" (
	echo Git repository already exists.
) else (
	git init -b main || exit /b 1
)

if not exist "%~dp0LocalConfig.bat" (
	copy "%~dp0LocalConfig.example.bat" "%~dp0LocalConfig.bat" > nul
	echo Created Scripts\LocalConfig.bat - edit AGE_PROJECT before running Export.bat.
)

git status --short

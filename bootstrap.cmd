@echo off
rem bootstrap.cmd - cmd.exe entry point. Runs bootstrap.ps1 via PowerShell,
rem which ships with every supported version of Windows -- no Git Bash, WSL,
rem or anything else needs to be installed first, beyond git itself (already
rem required to have cloned this repo).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0bootstrap.ps1" %*
exit /b %errorlevel%

@echo off
rem bootstrap.cmd - cmd.exe launcher for bootstrap.sh.
rem
rem bootstrap.sh is a bash script (it uses bash arrays, process substitution-free
rem POSIX constructs, and shells out to git/powershell). cmd.exe cannot run .sh
rem files directly, so this finds a bash.exe (Git for Windows ships one) and
rem re-invokes bootstrap.sh through it, forwarding this script's exit code.
setlocal enabledelayedexpansion

set "BASH_EXE="

where bash.exe >nul 2>nul
if %errorlevel%==0 (
    for /f "delims=" %%i in ('where bash.exe') do (
        if not defined BASH_EXE set "BASH_EXE=%%i"
    )
)

if not defined BASH_EXE (
    for %%P in (
        "%ProgramFiles%\Git\bin\bash.exe"
        "%ProgramFiles(x86)%\Git\bin\bash.exe"
        "%ProgramFiles%\Git\usr\bin\bash.exe"
        "%ProgramW6432%\Git\bin\bash.exe"
    ) do (
        if not defined BASH_EXE if exist %%P set "BASH_EXE=%%~P"
    )
)

if not defined BASH_EXE (
    echo error: could not find bash.exe. Install Git for Windows ^(https://git-scm.com/download/win^) and re-run.
    exit /b 1
)

"%BASH_EXE%" "%~dp0bootstrap.sh" %*
exit /b %errorlevel%

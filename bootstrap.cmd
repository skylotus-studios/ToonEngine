@echo off
rem bootstrap.cmd - cmd.exe launcher for bootstrap.sh.
rem
rem bootstrap.sh is a bash script (it uses bash arrays, process substitution-free
rem POSIX constructs, and shells out to git/powershell). cmd.exe cannot run .sh
rem files directly, so this finds Git for Windows' bash.exe and re-invokes
rem bootstrap.sh through it, forwarding this script's exit code.
rem
rem Deliberately prefers the known Git-for-Windows install paths over a bare
rem `where bash.exe`: on machines with WSL also installed, `where` can return
rem the WSL bash launcher stub (under System32 or WindowsApps) ahead of Git's.
rem That stub treats a Windows path like "C:\foo\bootstrap.sh" as a literal
rem filename (backslashes aren't path separators to it) and fails with
rem "No such file or directory" -- so it is explicitly skipped below.
setlocal enabledelayedexpansion

set "BASH_EXE="

for %%P in (
    "%ProgramFiles%\Git\bin\bash.exe"
    "%ProgramFiles(x86)%\Git\bin\bash.exe"
    "%ProgramFiles%\Git\usr\bin\bash.exe"
    "%ProgramW6432%\Git\bin\bash.exe"
) do (
    if not defined BASH_EXE if exist %%P set "BASH_EXE=%%~P"
)

if not defined BASH_EXE (
    where bash.exe >nul 2>nul
    if !errorlevel!==0 (
        for /f "delims=" %%i in ('where bash.exe') do (
            echo %%i | findstr /i /c:"\System32\" /c:"\WindowsApps\" >nul
            if errorlevel 1 if not defined BASH_EXE set "BASH_EXE=%%i"
        )
    )
)

if not defined BASH_EXE (
    echo error: could not find a real bash.exe ^(only WSL's stub, if any^). Install Git for Windows ^(https://git-scm.com/download/win^) and re-run.
    exit /b 1
)

"%BASH_EXE%" "%~dp0bootstrap.sh" %*
exit /b %errorlevel%

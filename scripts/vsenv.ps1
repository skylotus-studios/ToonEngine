#Requires -Version 5
<#
    vsenv.ps1 — import the Visual Studio Developer environment into the current
    PowerShell session.

    ToonEngine builds with Ninja + clang-cl, which need the Windows SDK tools
    (mt.exe, rc.exe) and the MSVC import libraries on PATH. A plain shell does
    not have them, so CMake configure fails at CMAKE_MT-NOTFOUND. This script
    puts them there.

    Portable across VS editions/versions: it locates the install with vswhere,
    then sources VsDevCmd.bat and copies the resulting environment in. (Note:
    Launch-VsDevShell.ps1 is NOT used — its parameter set varies between VS
    builds; VsDevCmd.bat's args are stable back to VS 2017.)

    Dot-source it so the environment sticks in your session:

        . .\scripts\vsenv.ps1
        cmake --preset windows-debug

    CLion users don't need this by hand: a CLion "Visual Studio" toolchain sources
    the VS Developer environment automatically (see docs/clion-setup.md). This
    script is for command-line / CI builds from a plain PowerShell.
#>
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio 2022 with the 'Desktop development with C++' workload (Windows SDK + clang tools)."
}

$vsPath = (& $vswhere -latest -products * -property installationPath) | Select-Object -First 1
if (-not $vsPath) { throw "vswhere found no Visual Studio installation." }

$vsDevCmd = Join-Path $vsPath 'Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $vsDevCmd)) { throw "VsDevCmd.bat not found at '$vsDevCmd'." }

# Run VsDevCmd.bat in a child cmd, dump the resulting environment with `set`,
# and import every variable into this PowerShell session.
cmd /c "`"$vsDevCmd`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') {
        Set-Item -Path "env:$($matches[1])" -Value $matches[2]
    }
}

# Fail loudly if the import didn't take — otherwise CMake fails later with the
# cryptic CMAKE_MT-NOTFOUND and it's not obvious why.
if (-not (Get-Command mt.exe -ErrorAction SilentlyContinue)) {
    throw "VS Developer environment import failed: mt.exe is still not on PATH."
}

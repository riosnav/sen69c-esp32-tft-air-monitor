# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds screen-renderer.exe with Visual Studio's C++ tools (MSVC, CMake and
# Ninja, from the "Desktop development with C++" workload). Run it again
# after the firmware's UI code changes.
param(
  # Outside the project, which may be in a synced folder.
  [string]$BuildDir = (Join-Path $env:LOCALAPPDATA 'sen69c-air-monitor\screen-renderer')
)
$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ tools found.' }

# Puts cl, cmake and ninja on the PATH, for this session only. Visual Studio's
# own setup script looks for vswhere on the PATH too.
$env:Path = "$(Split-Path $vswhere);$env:Path"
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

cmake -S $PSScriptRoot -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }
Write-Host "Built $(Join-Path $BuildDir 'screen-renderer.exe')"

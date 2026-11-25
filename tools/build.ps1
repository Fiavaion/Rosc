# Builds the host project's editor target, which compiles the Rosc plugin.
# Usage: powershell -File tools/build.ps1 [-Configuration Development]
param([string]$Configuration = "Development")

$ueRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }
$repo = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $repo "HostProject\RoscHost.uproject"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir "build.log"

$buildBat = Join-Path $ueRoot "Engine\Build\BatchFiles\Build.bat"
# -NoHotReloadFromIDE: UnrealBuildTool otherwise refuses to build while any editor of this engine
# install has Live Coding on, even one running a different project (LESSON-BUILD-002). If the
# Rosc host project itself is open, the link step still fails because its DLLs are in use.
cmd /c "`"$buildBat`" RoscHostEditor Win64 $Configuration -project=`"$uproject`" -waitmutex -NoHotReloadFromIDE > `"$log`" 2>&1"
$code = $LASTEXITCODE

# Show only compiler and linker diagnostics; the full log stays on disk.
Select-String -Path $log -Pattern "\): (error|warning) |: fatal error |error LNK|^ERROR:|Unhandled exception|Result: " |
	Select-Object -First 80 | ForEach-Object { $_.Line }

Write-Output "build exit code: $code   log: $log"
exit $code

# Packages the plugin the way Fab does (RunUAT BuildPlugin), to catch errors that only show
# up outside the host project. Output goes to .scratch/Packaged (gitignored).
# Usage: powershell -File tools/package.ps1

$ueRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }
$repo = Split-Path -Parent $PSScriptRoot
$uplugin = Join-Path $repo "HostProject\Plugins\UEcine\UEcine.uplugin"
$outDir = Join-Path $repo ".scratch\Packaged\UEcine"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
$log = Join-Path $logDir "package.log"

$runUat = Join-Path $ueRoot "Engine\Build\BatchFiles\RunUAT.bat"
cmd /c "`"$runUat`" BuildPlugin -Plugin=`"$uplugin`" -Package=`"$outDir`" -TargetPlatforms=Win64 -Rocket > `"$log`" 2>&1"
$code = $LASTEXITCODE

Select-String -Path $log -Pattern "\): (error|warning) |: fatal error |error LNK|^ERROR:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting" |
	Select-Object -First 80 | ForEach-Object { $_.Line }

Write-Output "package exit code: $code   log: $log"
exit $code

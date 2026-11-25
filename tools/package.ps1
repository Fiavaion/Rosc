# Packages the plugin the way Fab does (RunUAT BuildPlugin), to catch errors that only show
# up outside the host project. Output goes to .scratch/Packaged (gitignored).
# Usage: powershell -File tools/package.ps1

$ueRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }
$repo = Split-Path -Parent $PSScriptRoot
$uplugin = Join-Path $repo "HostProject\Plugins\Rosc\Rosc.uplugin"
$outDir = Join-Path $repo ".scratch\Packaged\Rosc"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
$log = Join-Path $logDir "package.log"

$runUat = Join-Path $ueRoot "Engine\Build\BatchFiles\RunUAT.bat"
cmd /c "`"$runUat`" BuildPlugin -Plugin=`"$uplugin`" -Package=`"$outDir`" -TargetPlatforms=Win64 -Rocket > `"$log`" 2>&1"
$code = $LASTEXITCODE

Select-String -Path $log -Pattern "\): (error|warning) |: fatal error |error LNK|^ERROR:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting" |
	Select-Object -First 80 | ForEach-Object { $_.Line }

# What a buyer needs must be in the package, and every source file must say whose it is.
if ($code -eq 0) {
	$needed = @(
		"Rosc.uplugin", "THIRD_PARTY_NOTICES.md", "Docs\UserGuide.html", "Docs\images\panel-camera.png", "Docs\images\sample-stage-peaking.png",
		"Content\Sample\RoscSampleStage.umap", "Resources\Icon128.png", "Resources\Icons\Panel.svg",
		"Resources\Database\lenses.json", "Resources\AI\models.json", "Resources\AI\rosc_ai_server.py",
		"Shaders\Private\RoscMonitor.usf", "Binaries\Win64\UnrealEditor-RoscEditor.dll"
	)
	$missing = @($needed | Where-Object { -not (Test-Path (Join-Path $outDir $_)) })
	$unsigned = @(Get-ChildItem -Recurse -File (Join-Path $outDir "Source"), (Join-Path $outDir "Shaders") -Include *.h, *.cpp, *.cs, *.usf |
		Where-Object { (Get-Content $_.FullName -TotalCount 1) -notmatch "^// Copyright \d{4} " })
	$missing | ForEach-Object { Write-Output "MISSING from the package: $_" }
	$unsigned | ForEach-Object { Write-Output "NO COPYRIGHT NOTICE: $($_.FullName)" }
	$files = @(Get-ChildItem -Recurse -File $outDir).Count
	Write-Output ("package contents: {0} files, {1} missing, {2} source files without a copyright notice" -f $files, $missing.Count, $unsigned.Count)
	if ($missing.Count -gt 0 -or $unsigned.Count -gt 0) { $code = 1 }
}

Write-Output "package exit code: $code   log: $log"
exit $code

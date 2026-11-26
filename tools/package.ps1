# Packages the plugin the way Fab does (RunUAT BuildPlugin), to catch errors that only show
# up outside the host project, then installs the package in a new Blueprint-only project and
# runs the Rosc tests there, as a user who has no C++ toolchain would have it.
# Output goes to .scratch/Packaged and .scratch/InstallCheck (gitignored).
# Usage: powershell -File tools/package.ps1 [-Rendering]
#   -Rendering   run the installed copy's tests with a real GPU device, as tools/test.ps1 does.
param(
	[switch]$Rendering
)

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

# What a user needs must be in the package, and every source file must say whose it is and its licence.
if ($code -eq 0) {
	$needed = @(
		"Rosc.uplugin", "LICENSE", "THIRD_PARTY_NOTICES.md", "Docs\UserGuide.html", "Docs\TrainingData.html", "Docs\guide.css", "Docs\images\panel-camera.png", "Docs\images\sample-stage-peaking.png",
		"Content\Sample\RoscSampleStage.umap", "Resources\Icon128.png", "Resources\Icons\Panel.svg",
		"Resources\Database\lenses.json", "Resources\AI\models.json", "Resources\AI\rosc_ai_server.py",
		"Shaders\Private\RoscMonitor.usf", "Binaries\Win64\UnrealEditor-RoscEditor.dll"
	)
	$missing = @($needed | Where-Object { -not (Test-Path (Join-Path $outDir $_)) })
	$unsigned = @(Get-ChildItem -Recurse -File (Join-Path $outDir "Source"), (Join-Path $outDir "Shaders") -Include *.h, *.cpp, *.cs, *.usf |
		Where-Object { ((Get-Content $_.FullName -TotalCount 2) -join "`n") -notmatch "^// Copyright \d{4} Fiavaion`n// SPDX-License-Identifier: Apache-2.0$" })
	$missing | ForEach-Object { Write-Output "MISSING from the package: $_" }
	$unsigned | ForEach-Object { Write-Output "NO COPYRIGHT AND LICENCE NOTICE: $($_.FullName)" }
	$files = @(Get-ChildItem -Recurse -File $outDir).Count
	Write-Output ("package contents: {0} files, {1} missing, {2} source files without a copyright and licence notice" -f $files, $missing.Count, $unsigned.Count)
	if ($missing.Count -gt 0 -or $unsigned.Count -gt 0) { $code = 1 }
}

# The package in a project of its own: nothing but a .uproject that turns Rosc on.
if ($code -eq 0) {
	$checkDir = Join-Path $repo ".scratch\InstallCheck"
	if (Test-Path $checkDir) { Remove-Item -Recurse -Force $checkDir }
	New-Item -ItemType Directory -Force (Join-Path $checkDir "Plugins") | Out-Null
	Copy-Item -Recurse $outDir (Join-Path $checkDir "Plugins\Rosc")
	$checkProject = Join-Path $checkDir "RoscInstallCheck.uproject"
	Set-Content -Path $checkProject -Encoding utf8 -Value '{ "FileVersion": 3, "EngineAssociation": "5.8", "Plugins": [ { "Name": "Rosc", "Enabled": true } ] }'
	Write-Output "installed in a new project: $checkProject"
	& (Join-Path $PSScriptRoot "test.ps1") -Project $checkProject -Rendering:$Rendering
	if ($LASTEXITCODE -ne 0) { $code = 1 }
}

Write-Output "package exit code: $code   log: $log"
exit $code

# Packages the plugin the way Fab does (RunUAT BuildPlugin), to catch errors that only show
# up outside the host project, then installs the package in a new Blueprint-only project and
# runs the Rosc tests there, as a user who has no C++ toolchain would have it. What passes is
# zipped as the ready-built download (Rosc-<version>-UE<major.minor>-Win64.zip).
# Output goes to .scratch/Packaged and .scratch/InstallCheck (gitignored).
# Usage: powershell -File tools/package.ps1 [-Rendering]
#   -Rendering   run the installed copy's tests with a real GPU device, as tools/test.ps1 does.
param(
	[switch]$Rendering
)

$ueRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }
$repo = Split-Path -Parent $PSScriptRoot
$uplugin = Join-Path $repo "HostProject\Plugins\Rosc\Rosc.uplugin"
$descriptor = Get-Content $uplugin -Raw | ConvertFrom-Json
$stage = Join-Path $repo ".scratch\Packaged"
$outDir = Join-Path $stage "Rosc"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir, $stage | Out-Null
if (Test-Path $outDir) { Remove-Item -Recurse -Force $outDir }
$log = Join-Path $logDir "package.log"

# The compiler writes the path of each source file into the binaries. Building through a drive
# letter of its own keeps the folders of this machine out of a package that is handed to others.
$drive = @("R", "S", "T", "U", "W", "X", "Y", "Z") | Where-Object { -not (Test-Path "${_}:\") } | Select-Object -First 1
if (-not $drive) { Write-Output "no free drive letter to build through"; exit 1 }
subst "${drive}:" $stage
$runUat = Join-Path $ueRoot "Engine\Build\BatchFiles\RunUAT.bat"
try {
	cmd /c "`"$runUat`" BuildPlugin -Plugin=`"$uplugin`" -Package=`"${drive}:\Rosc`" -TargetPlatforms=Win64 -Rocket > `"$log`" 2>&1"
	$code = $LASTEXITCODE
} finally {
	subst "${drive}:" /D
}

Select-String -Path $log -Pattern "\): (error|warning) |: fatal error |error LNK|^ERROR:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting" |
	Select-Object -First 80 | ForEach-Object { $_.Line }

# A user needs neither the build's intermediate files nor its debug symbols, and nothing handed
# out may name a folder of this machine.
if ($code -eq 0) {
	Remove-Item -Recurse -Force (Join-Path $outDir "Intermediate") -ErrorAction SilentlyContinue
	Get-ChildItem -Recurse -File $outDir -Filter *.pdb | Remove-Item -Force
	$private = @($repo, $env:USERPROFILE) | Where-Object { $_ } | ForEach-Object { $_; $_.Replace("\", "/") }
	$latin1 = [System.Text.Encoding]::GetEncoding(28591)
	$named = @(Get-ChildItem -Recurse -File $outDir | Where-Object {
		$bytes = [System.IO.File]::ReadAllBytes($_.FullName)
		$texts = @($latin1.GetString($bytes), [System.Text.Encoding]::Unicode.GetString($bytes))
		if ($bytes.Length -gt 1) { $texts += [System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
		@($texts | Where-Object { $t = $_; @($private | Where-Object { $t.IndexOf($_, [StringComparison]::OrdinalIgnoreCase) -ge 0 }).Count -gt 0 }).Count -gt 0
	})
	$named | ForEach-Object { Write-Output "NAMES A FOLDER OF THIS MACHINE: $($_.FullName)" }
	if ($named.Count -gt 0) { $code = 1 }
}

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

if ($code -eq 0) {
	$engine = ($descriptor.EngineVersion -split "\.")[0..1] -join "."
	$zip = Join-Path $stage ("Rosc-{0}-UE{1}-Win64.zip" -f $descriptor.VersionName, $engine)
	# Entry names are written by hand with "/": Windows PowerShell's Compress-Archive and
	# ZipFile.CreateFromDirectory both write "\" into them, which unzip tools off Windows misread.
	Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
	if (Test-Path $zip) { Remove-Item -Force $zip }
	$root = Split-Path -Parent $outDir
	$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
	try {
		Get-ChildItem -Recurse -File $outDir | ForEach-Object {
			$name = $_.FullName.Substring($root.Length + 1).Replace("\", "/")
			[void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $name, [System.IO.Compression.CompressionLevel]::Optimal)
		}
	} finally {
		$archive.Dispose()
	}
	Write-Output ("download: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
}

Write-Output "package exit code: $code   log: $log"
exit $code

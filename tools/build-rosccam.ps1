# Builds the RoscCam app for Android from RoscCam/: checks and tests its code, then makes the APK.
# Usage: powershell -File tools/build-rosccam.ps1 [-Debug]
#   -Debug  a debug build, with the hooks the emulator test drives (tools/test-rosccam.ps1)
# Needs Flutter (on PATH, or FLUTTER_ROOT set) and the Android SDK (ANDROID_HOME).
param([switch]$Debug)

$repo = Split-Path -Parent $PSScriptRoot
$app = Join-Path $repo "RoscCam"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
$log = Join-Path $logDir "rosccam-build.log"

$flutter = if ($env:FLUTTER_ROOT) { Join-Path $env:FLUTTER_ROOT "bin\flutter.bat" } else { (Get-Command flutter -ErrorAction SilentlyContinue).Source }
if (-not $flutter -or -not (Test-Path $flutter)) {
	Write-Output "Flutter not found: put it on PATH or set FLUTTER_ROOT to the Flutter SDK's folder."
	exit 1
}
if (-not $env:ANDROID_HOME) {
	Write-Output "ANDROID_HOME is not set: set it to the Android SDK's folder."
	exit 1
}

# Runs one Flutter command with its output added to the log; returns its exit code.
function Invoke-Flutter([string]$Arguments) {
	Add-Content -Path $log -Value "== flutter $Arguments" -Encoding utf8
	cmd /c "`"$flutter`" $Arguments >> `"$log`" 2>&1"
	return $LASTEXITCODE
}

Push-Location $app
try {
	Set-Content -Path $log -Value "RoscCam build" -Encoding utf8
	if ((Invoke-Flutter "pub get") -ne 0) { Write-Output "pub get failed   log: $log"; exit 1 }

	if ((Invoke-Flutter "analyze") -ne 0) {
		Select-String -Path $log -Pattern "^\s*(error|warning|info) - " | Select-Object -First 40 | ForEach-Object { $_.Line }
		Write-Output "analyze found problems   log: $log"
		exit 1
	}

	if ((Invoke-Flutter "test") -ne 0) {
		Select-String -Path $log -Pattern "\[E\]|Expected:|Actual:" | Select-Object -First 40 | ForEach-Object { $_.Line }
		Write-Output "app tests failed   log: $log"
		exit 1
	}
	$tests = Select-String -Path $log -Pattern "All tests passed" | Select-Object -Last 1
	Write-Output "app tests: $(if ($tests) { $tests.Line.Trim() } else { 'passed' })"

	$mode = if ($Debug) { "debug" } else { "release" }
	$code = Invoke-Flutter "build apk --$mode"
	$apk = Join-Path $app "build\app\outputs\flutter-apk\app-$mode.apk"
	if ($code -ne 0 -or -not (Test-Path $apk)) {
		Select-String -Path $log -Pattern "error|FAILURE|What went wrong" | Select-Object -First 30 | ForEach-Object { $_.Line }
		Write-Output "build failed   log: $log"
		exit 1
	}
	$size = [Math]::Round((Get-Item $apk).Length / 1MB, 1)
	Write-Output "apk: $apk ($size MB)   log: $log"
	exit 0
}
finally {
	Pop-Location
}

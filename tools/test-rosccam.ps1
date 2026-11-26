# Runs RoscCam end to end on the Android emulator against a headless editor: the app connects and
# pairs, the device is turned 30 degrees right and the support has to pan by as much, and the
# device's screen has to show the camera's picture (the editor test Rosc.Editor.RoscCam.Device).
# Usage: powershell -File tools/test-rosccam.ps1 [-Avd name] [-KeepEmulator]
#   -Avd           the emulator to start when none is running (default: the first one listed)
#   -KeepEmulator  leave an emulator this script started running afterwards
# Needs what tools/build-rosccam.ps1 needs, an Android emulator, and what tools/test.ps1 needs.
param([string]$Avd = "", [switch]$KeepEmulator)

$repo = Split-Path -Parent $PSScriptRoot
if (-not $env:ANDROID_HOME) { Write-Output "ANDROID_HOME is not set: set it to the Android SDK's folder."; exit 1 }
$adb = Join-Path $env:ANDROID_HOME "platform-tools\adb.exe"
$emulatorExe = Join-Path $env:ANDROID_HOME "emulator\emulator.exe"
$package = "com.fiavaion.rosccam"
$folder = Join-Path $repo ".scratch\rosccam-e2e"
$logDir = Join-Path $repo ".scratch\logs"
New-Item -ItemType Directory -Force $logDir | Out-Null

function Send-Hook([string]$Action, [string]$Extras) {
	& $adb shell am broadcast -p $package -a "$package.$Action" $Extras.Split(" ") | Out-Null
}
function Test-Ready([string]$Name) { Test-Path (Join-Path $folder $Name) }

# 1. The app, as a debug build: only that has the hooks this script drives.
powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "build-rosccam.ps1") -Debug
if ($LASTEXITCODE -ne 0) { exit 1 }
$apk = Join-Path $repo "RoscCam\build\app\outputs\flutter-apk\app-debug.apk"

# 2. A device: the running emulator, or one started here.
$started = $false
if (-not ((& $adb devices) -match "^emulator-\d+\s+device")) {
	if (-not $Avd) { $Avd = (& $emulatorExe -list-avds | Select-Object -First 1) }
	if (-not $Avd) { Write-Output "No emulator is set up: make one in Android Studio's Device Manager."; exit 1 }
	Write-Output "starting emulator $Avd"
	Start-Process -FilePath $emulatorExe -ArgumentList "-avd $Avd -no-window -no-audio -no-snapshot-save -gpu swiftshader_indirect -no-boot-anim" -WindowStyle Hidden
	$started = $true
	& $adb wait-for-device
	$deadline = (Get-Date).AddMinutes(3)
	while ((& $adb shell getprop sys.boot_completed 2>$null) -notmatch "1") {
		if ((Get-Date) -gt $deadline) { Write-Output "The emulator did not finish starting."; exit 1 }
		Start-Sleep -Seconds 2
	}
}
# Android tells a new full-screen app how to leave full screen, over the picture; say it was seen.
& $adb shell settings put secure immersive_mode_confirmations confirmed
& $adb install -r -t $apk | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Output "The app did not install on the emulator."; exit 1 }
& $adb shell am force-stop $package
& $adb shell am start -n "$package/.MainActivity" | Out-Null

# 3. The editor's side, in the background; the two take turns through the files in $folder.
if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
New-Item -ItemType Directory -Force $folder | Out-Null
$testOut = Join-Path $logDir "rosccam-e2e.txt"
$editor = Start-Process -FilePath "powershell" -PassThru -WindowStyle Hidden -RedirectStandardOutput $testOut -ArgumentList @(
	"-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "test.ps1"),
	"-Filter", "Rosc.Editor.RoscCam.Device", "-Rendering", "-EditorArgs", "`"-RoscCamDevice=$folder`"")
# Without a look at the handle now, PowerShell does not keep the exit code of a process started so.
$null = $editor.Handle

# 4. The app: connect (the emulator reaches the PC at 10.0.2.2), pair, hold level, turn, grab.
$connectSent = [DateTime]::MinValue
$paired = $false
$turned = $false
$grabbed = $false
$deadline = (Get-Date).AddMinutes(6)
while (-not $editor.HasExited) {
	if ((Get-Date) -gt $deadline) { Write-Output "The run took too long."; Stop-Process -Id $editor.Id -Force; break }
	if ((Test-Ready "running.txt") -and -not (Test-Ready "code.txt") -and ((Get-Date) - $connectSent).TotalSeconds -gt 5) {
		Send-Hook "TEST_CONNECT" "--es address 10.0.2.2"
		$connectSent = Get-Date
	}
	if ((Test-Ready "code.txt") -and -not $paired) {
		Send-Hook "TEST_ROTATION" "--ef right 0 --ef up 0 --ef roll 0"
		Send-Hook "TEST_PAIR" ("--es code " + (Get-Content (Join-Path $folder "code.txt") -Raw).Trim())
		$paired = $true
	}
	if ((Test-Ready "operating.txt") -and -not $turned) {
		Send-Hook "TEST_ROTATION" "--ef right 30 --ef up 0 --ef roll 0"
		$turned = $true
	}
	if ((Test-Ready "panned.txt") -and -not $grabbed) {
		# A moment for the turned picture to reach the device.
		Start-Sleep -Seconds 2
		cmd /c "`"$adb`" exec-out screencap -p > `"$(Join-Path $folder 'screen.png')`""
		((& $adb shell wm density) | Select-Object -Last 1) -replace "\D", "" | Set-Content (Join-Path $folder "density.txt")
		Set-Content (Join-Path $folder "grabbed.txt") "The screen is grabbed."
		$grabbed = $true
	}
	Start-Sleep -Milliseconds 250
}
$editor.WaitForExit()
Get-Content $testOut | ForEach-Object { $_ }
Select-String -Path (Join-Path $logDir "test.log") -Pattern "asks to pair|tripod panned|Screen \d+ x|differs from the camera" | ForEach-Object { $_.Line -replace "^.*LogAutomationController: ", "  " }
$code = $editor.ExitCode

if ($started -and -not $KeepEmulator) { & $adb emu kill | Out-Null }
Write-Output "screen grab: $(Join-Path $folder 'screen.png')"
exit $code

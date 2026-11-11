# Runs UEcine automation tests in a headless editor and prints pass/fail counts.
# Usage: powershell -File tools/test.ps1 [-Filter UEcine.Core.Optics] [-Rendering]
#   -Rendering  start the editor with a real GPU device (needed by tests that render frames).
param(
	[string]$Filter = "UEcine",
	[switch]$Rendering
)

$ueRoot = if ($env:UE_ROOT) { $env:UE_ROOT } else { "D:\Program Files\Epic Games\UE_5.8" }
$repo = Split-Path -Parent $PSScriptRoot
$uproject = Join-Path $repo "HostProject\UEcineHost.uproject"
$logDir = Join-Path $repo ".scratch\logs"
$reportDir = Join-Path $repo ".scratch\test-report"
New-Item -ItemType Directory -Force $logDir | Out-Null
if (Test-Path $reportDir) { Remove-Item -Recurse -Force $reportDir }
$log = Join-Path $logDir "test.log"

$editor = Join-Path $ueRoot "Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$rhi = if ($Rendering) { "-RenderOffscreen" } else { "-nullrhi" }
$execCmds = "Automation RunTests $Filter; Quit"
cmd /c "`"$editor`" `"$uproject`" -ExecCmds=`"$execCmds`" -TestExit=`"Automation Test Queue Empty`" -ReportExportPath=`"$reportDir`" -unattended -nopause -nosplash -nosound $rhi -stdout -FullStdOutLogOutput > `"$log`" 2>&1"

$reportFile = Join-Path $reportDir "index.json"
if (-not (Test-Path $reportFile)) {
	Write-Output "FAIL: no test report was written (the editor may have failed to start). Log: $log"
	Select-String -Path $log -Pattern "Error:|Fatal error|No automation tests matched" | Select-Object -First 20 | ForEach-Object { $_.Line }
	exit 2
}

$report = Get-Content $reportFile -Raw | ConvertFrom-Json
foreach ($test in $report.tests) {
	if ($test.state -ne "Success") {
		Write-Output ("FAILED  " + $test.fullTestPath)
		foreach ($entry in $test.entries) {
			if ($entry.event.type -ne "Info") { Write-Output ("        " + $entry.event.type + ": " + $entry.event.message) }
		}
	}
}

$total = @($report.tests).Count
Write-Output ("tests: {0}   passed: {1}   warnings: {2}   failed: {3}   not run: {4}   filter: {5}" -f $total, $report.succeeded, $report.succeededWithWarnings, $report.failed, $report.notRun, $Filter)
if ($total -eq 0 -or $report.failed -gt 0 -or $report.notRun -gt 0) { exit 1 }
exit 0

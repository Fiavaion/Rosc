# Runs UEcine automation tests in a headless editor and prints pass/fail counts.
# Usage: powershell -File tools/test.ps1 [-Filter UEcine.Core.Optics] [-Rendering] [-EditorArgs "..."]
#   -Rendering   start the editor with a real GPU device (needed by tests that render frames).
#   -EditorArgs  more arguments for the editor's command line (tools/evals.ps1 uses this).
param(
	[string]$Filter = "UEcine",
	[switch]$Rendering,
	[string]$EditorArgs = ""
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
cmd /c "`"$editor`" `"$uproject`" -ExecCmds=`"$execCmds`" -TestExit=`"Automation Test Queue Empty`" -ReportExportPath=`"$reportDir`" -unattended -nopause -nosplash -nosound $rhi $EditorArgs -stdout -FullStdOutLogOutput > `"$log`" 2>&1"

$reportFile = Join-Path $reportDir "index.json"
if (-not (Test-Path $reportFile)) {
	Write-Output "FAIL: no test report was written (the editor may have failed to start). Log: $log"
	Select-String -Path $log -Pattern "Error:|Fatal error|No automation tests matched" | Select-Object -First 20 | ForEach-Object { $_.Line }
	exit 2
}

$report = Get-Content $reportFile -Raw | ConvertFrom-Json
# Show what went wrong, but only the first few lines per test: one broken thing often fails
# on every frame it is checked, and the full list is in the report file.
$maxLines = 12
foreach ($test in $report.tests) {
	$problems = @($test.entries | Where-Object { $_.event.type -ne "Info" })
	if ($test.state -ne "Success" -or $problems.Count -gt 0) {
		$label = if ($test.state -ne "Success") { "FAILED  " } else { "WARNING " }
		Write-Output ($label + $test.fullTestPath)
		$problems | Select-Object -First $maxLines | ForEach-Object { Write-Output ("        " + $_.event.type + ": " + $_.event.message) }
		if ($problems.Count -gt $maxLines) { Write-Output ("        ... and {0} more in {1}" -f ($problems.Count - $maxLines), $reportFile) }
	}
}

$total = @($report.tests).Count
Write-Output ("tests: {0}   passed: {1}   warnings: {2}   failed: {3}   not run: {4}   filter: {5}" -f $total, $report.succeeded, $report.succeededWithWarnings, $report.failed, $report.notRun, $Filter)
if ($total -eq 0 -or $report.failed -gt 0 -or $report.notRun -gt 0) { exit 1 }
exit 0

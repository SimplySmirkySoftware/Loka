param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable
)

$ErrorActionPreference = "Stop"
$workDirectory = Join-Path $env:TEMP ("loka-win32-assert-" + [Guid]::NewGuid().ToString("N"))
$stdoutPath = Join-Path $workDirectory "stdout.txt"
$stderrPath = Join-Path $workDirectory "stderr.txt"
$process = $null

New-Item -ItemType Directory -Path $workDirectory | Out-Null
try {
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    # -NoNewWindow keeps the console child in this console. Without it an
    # interactive session gives the child its own console window, which is
    # the process's MainWindowHandle and read as a dialog below (#1210).
    $process = Start-Process `
        -FilePath $TestExecutable `
        -ArgumentList "--win32-assert-probe" `
        -NoNewWindow `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru
    # Force System.Diagnostics.Process to retain a native handle so ExitCode
    # remains queryable after this short-lived child has terminated.
    $null = $process.Handle

    # The deadline bounds a hang that shows no window; the window check below
    # is independent of it. A loaded hosted runner has needed more than 5 s to
    # finish tearing down a process whose assertion had already fired (#919).
    $deadlineSeconds = 30
    $deadline = [DateTime]::UtcNow.AddSeconds($deadlineSeconds)
    $dialogHandle = [IntPtr]::Zero
    while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $dialogHandle = $process.MainWindowHandle
            break
        }
        Start-Sleep -Milliseconds 100
    }

    if ($dialogHandle -ne [IntPtr]::Zero) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        $dialogDiagnostic = [IO.File]::ReadAllText($stderrPath)
        throw "Win32 assert probe displayed a process window. stderr: $dialogDiagnostic"
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
        $timedOutDiagnostic = [IO.File]::ReadAllText($stderrPath)
        throw "Win32 assert probe did not exit within $deadlineSeconds seconds. stderr: $timedOutDiagnostic"
    }

    $process.WaitForExit()
    $exitMilliseconds = $stopwatch.ElapsedMilliseconds
    $exitCode = $process.ExitCode
    $diagnostic = [IO.File]::ReadAllText($stderrPath)
    if ($null -eq $exitCode) {
        throw "Win32 assert probe exit code was unavailable."
    }
    if ($exitCode -eq 0) {
        throw "Win32 assert probe exited successfully instead of aborting."
    }
    if ($diagnostic -notmatch "Assertion failed:") {
        throw "Win32 assert probe did not write the CRT assertion diagnostic to stderr: $diagnostic"
    }
    if ($diagnostic -notmatch "intentional Win32 assert probe") {
        throw "Win32 assert probe diagnostic did not identify the positive control: $diagnostic"
    }

    Write-Host "Win32 assert exited $exitCode after $exitMilliseconds ms without displaying a dialog."
}
finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    Remove-Item -LiteralPath $workDirectory -Recurse -Force
}

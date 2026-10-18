param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Example,
    [Parameter(Position = 1, Mandatory = $true)]
    [string]$Scenario,
    [Alias("update-golden")]
    [switch]$UpdateGolden
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$ProjectDirectory = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Registry = Join-Path $ProjectDirectory "tests/scenarios/scenarios.txt"
$StartupIdentityDeclarations = Join-Path $ProjectDirectory "tests/scenarios/startup-golden-identities.txt"
$FixtureRegistry = Join-Path $ProjectDirectory "tests/scenarios/scrapbook-package-fixtures.txt"
$ExpectedAudit = Join-Path $ProjectDirectory "tests/scenarios/expected/$Example/$Scenario.audit"
# SmirkBench's historical startup audit contains Toolbox-only paint counters.
if ("$Example/$Scenario" -eq "smirkbench/startup") {
    $ExpectedAudit = Join-Path $ProjectDirectory "tests/scenarios/desktop-expected/smirkbench/startup.audit"
}
$Vehicles = @{
    "smirkbench" = @{ Executable = "LokaSmirkBenchScenarioWin32.exe"; OutputDirectory = "SmirkBench" }
    "scrapbook" = @{ Executable = "LokaScrapbookScenarioWin32.exe"; OutputDirectory = "ScrapbookUI" }
    "helloworld" = @{ Executable = "LokaHelloWorldScenarioWin32.exe"; OutputDirectory = "HelloWorld" }
    "tutorial" = @{ Executable = "LokaTutorialScenarioWin32.exe"; OutputDirectory = "Tutorial" }
    "minesweeper" = @{ Executable = "LokaMineSweeperScenarioWin32.exe"; OutputDirectory = "MineSweeper" }
    "floppybird" = @{ Executable = "LokaFloppyBirdScenarioWin32.exe"; OutputDirectory = "FloppyBird" }
}
# The remaining SmirkBench cells are Toolbox counter probes.
if ($Example -eq "smirkbench" -and $Scenario -notin @("startup", "attributed-editor-line")) {
    throw "SmirkBench scenario is Toolbox-only: $Scenario"
}
if ("$Example/$Scenario" -eq "helloworld/bmi-tab") {
    throw "HelloWorld bmi-tab is Toolbox-only"
}
$BuiltExecutable = $null
$Lrpc = Join-Path $ProjectDirectory "build/host/lrpc/lrpc.exe"
$MultiConfigLrpc = Join-Path $ProjectDirectory "build/host/lrpc/Debug/lrpc.exe"
$SourceAssets = Join-Path $ProjectDirectory "example/ScrapbookUI/assets/ASSETS-modern.LRP"
$Work = Join-Path $ProjectDirectory "build/win32-scenario/$Example/$Scenario"
$Stage = Join-Path $Work "stage"
$Settle = Join-Path $Work "settle"
# Filled in once the rig is known. A golden belongs to one rig, so the rig has
# to be part of where it is stored: two rigs can share a checkout now that the
# name is chosen rather than derived from the architecture, and two rigs that
# report the same capture profile can still settle on different pixels (#459).
# Keyed only by example, the second bake would overwrite the first and every
# later comparison would silently use whichever rig baked last.
$GoldenRoot = $null
$GoldenDirectory = $null
$Golden = $null
$GoldenProfile = $null
$PngTool = Join-Path $ProjectDirectory "tests/scenarios/pngtool.py"
$GoldenIdentityGuard = Join-Path $ProjectDirectory "scripts/rig/golden_identity_guard.py"
$PackageFixtureGuard = Join-Path $ProjectDirectory "scripts/rig/package_fixture_guard.py"
$CaptureProfileGuard = Join-Path $ProjectDirectory "scripts/rig/capture_profile_guard.py"
$RunnerLog = Join-Path $Work "runner.log"
$ChildProcess = $null
$ChildWindow = [IntPtr]::Zero
$ChildStdoutTask = $null
$ChildStderrTask = $null
$Python = $null
$UseWslPython = $false
$PngExitCode = 0
$PngCompareResult = $null
$GoldenIdentityExitCode = 0
$PackageFixtureExitCode = 0
$CaptureProfileExitCode = 0
$ExitCode = 0

function Fail-Stage([string]$StageName, [string]$Message) {
    throw "$StageName stage failed: $Message"
}

function Write-Runner([string]$Message) {
    Write-Output $Message
    if (Test-Path -LiteralPath $Work) {
        Add-Content -LiteralPath $RunnerLog -Value $Message -Encoding UTF8
    }
}

function Test-FilesEqual([string]$First, [string]$Second) {
    $firstStream = [System.IO.File]::OpenRead($First)
    try {
        $secondStream = [System.IO.File]::OpenRead($Second)
        try {
            if ($firstStream.Length -ne $secondStream.Length) {
                return $false
            }
            $firstBuffer = New-Object byte[] 65536
            $secondBuffer = New-Object byte[] 65536
            while ($true) {
                $firstCount = $firstStream.Read($firstBuffer, 0, $firstBuffer.Length)
                $secondCount = $secondStream.Read($secondBuffer, 0, $secondBuffer.Length)
                if ($firstCount -ne $secondCount) {
                    return $false
                }
                if ($firstCount -eq 0) {
                    return $true
                }
                for ($index = 0; $index -lt $firstCount; ++$index) {
                    if ($firstBuffer[$index] -ne $secondBuffer[$index]) {
                        return $false
                    }
                }
            }
        } finally {
            $secondStream.Dispose()
        }
    } finally {
        $firstStream.Dispose()
    }
}

. (Join-Path $PSScriptRoot "ScenarioProfile.ps1")
. (Join-Path $PSScriptRoot "RigDescriptor.ps1")

function Read-CaptureBounds([string]$Path) {
    $values = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        if ($line -notmatch '^([a-z_]+)=(-?[0-9]+)$') {
            Fail-Stage "crop" "invalid capture bounds line '$line'"
        }
        if ($values.ContainsKey($Matches[1])) {
            Fail-Stage "crop" "duplicate capture bounds key '$($Matches[1])'"
        }
        $values[$Matches[1]] = [long]$Matches[2]
    }
    $expectedKeys = @("bounds_version", "left", "top", "right", "bottom")
    if ($values.Count -ne $expectedKeys.Count) {
        Fail-Stage "crop" "capture bounds contain unexpected or missing keys"
    }
    foreach ($key in $expectedKeys) {
        if (-not $values.ContainsKey($key)) {
            Fail-Stage "crop" "capture bounds omit '$key'"
        }
    }
    if ($values["bounds_version"] -ne 1 `
        -or $values["left"] -lt 0 -or $values["top"] -lt 0 `
        -or $values["right"] -le $values["left"] `
        -or $values["bottom"] -le $values["top"]) {
        Fail-Stage "crop" "capture bounds are invalid"
    }
    return $values
}

function Resolve-Python {
    if ($env:PYTHON3) {
        $script:Python = $env:PYTHON3
        return
    }
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python -and $python.Source -notmatch '\\Microsoft\\WindowsApps\\python\.exe$') {
        $script:Python = $python.Source
        return
    }
    $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if ($wsl) {
        $script:UseWslPython = $true
        return
    }
    Fail-Stage "build" "a real python.exe or WSL python3 is required for exact PNG crop/compare"
}

function Convert-ToWslPath([string]$Path, [string]$StageName = "crop") {
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ($fullPath -notmatch '^([A-Za-z]):\\(.*)$') {
        Fail-Stage $StageName "WSL Python requires a drive-letter path, got '$fullPath'"
    }
    $drive = $Matches[1].ToLowerInvariant()
    $tail = $Matches[2].Replace('\', '/')
    return "/mnt/$drive/$tail"
}

. (Join-Path $PSScriptRoot "PngCompare.ps1")

function Invoke-PngCrop([string]$InputPath, [long]$Left, [long]$Top, [long]$Right, [long]$Bottom, [string]$OutputPath) {
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $PngTool) crop `
            (Convert-ToWslPath $InputPath) $Left $Top $Right $Bottom (Convert-ToWslPath $OutputPath)
    } else {
        & $Python $PngTool crop $InputPath $Left $Top $Right $Bottom $OutputPath
    }
    $script:PngExitCode = $LASTEXITCODE
}

function Invoke-PngDiff([string]$Expected, [string]$Actual, [string]$OutputPath) {
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $PngTool) diff `
            (Convert-ToWslPath $Expected) (Convert-ToWslPath $Actual) (Convert-ToWslPath $OutputPath)
    } else {
        & $Python $PngTool diff $Expected $Actual $OutputPath
    }
    $script:PngExitCode = $LASTEXITCODE
}

function Invoke-GoldenIdentityGuard([string]$Capture) {
    $UpdateArguments = @()
    if ($env:LOKA_GOLDEN_UPDATE_ROOT) {
        $UpdateRoot = $env:LOKA_GOLDEN_UPDATE_ROOT
        if ($UseWslPython) { $UpdateRoot = Convert-ToWslPath $UpdateRoot }
        $UpdateArguments = @("--update-root", $UpdateRoot)
    }
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $GoldenIdentityGuard) `
            --registry (Convert-ToWslPath $Registry) `
            --declarations (Convert-ToWslPath $StartupIdentityDeclarations) `
            --golden-root (Convert-ToWslPath $GoldenRoot) `
            --capture (Convert-ToWslPath $Capture) `
            --example $Example --scenario $Scenario @UpdateArguments
    } else {
        & $Python $GoldenIdentityGuard `
            --registry $Registry `
            --declarations $StartupIdentityDeclarations `
            --golden-root $GoldenRoot `
            --capture $Capture `
            --example $Example --scenario $Scenario @UpdateArguments
    }
    $script:GoldenIdentityExitCode = $LASTEXITCODE
}

function Invoke-PackageFixturePlan {
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $PackageFixtureGuard "stage") plan `
            --registry (Convert-ToWslPath $FixtureRegistry "stage") `
            --scenario $Scenario 2>&1
    } else {
        & $Python $PackageFixtureGuard plan `
            --registry $FixtureRegistry --scenario $Scenario 2>&1
    }
    $script:PackageFixtureExitCode = $LASTEXITCODE
}

function Invoke-PackageFixtureVerify([string]$StagedAssets) {
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $PackageFixtureGuard "stage") verify `
            --registry (Convert-ToWslPath $FixtureRegistry "stage") `
            --scenario $Scenario `
            --source (Convert-ToWslPath $SourceAssets "stage") `
            --staged (Convert-ToWslPath $StagedAssets "stage") 2>&1
    } else {
        & $Python $PackageFixtureGuard verify `
            --registry $FixtureRegistry --scenario $Scenario `
            --source $SourceAssets --staged $StagedAssets 2>&1
    }
    $script:PackageFixtureExitCode = $LASTEXITCODE
}

function Invoke-CaptureProfileGuard([string]$Descriptor, [string]$Profile) {
    if ($UseWslPython) {
        & wsl.exe python3 (Convert-ToWslPath $CaptureProfileGuard) `
            --descriptor (Convert-ToWslPath $Descriptor) `
            --profile (Convert-ToWslPath $Profile)
    } else {
        & $Python $CaptureProfileGuard `
            --descriptor $Descriptor `
            --profile $Profile
    }
    $script:CaptureProfileExitCode = $LASTEXITCODE
}

$NativeSource = Join-Path $PSScriptRoot "ScenarioCaptureNative.cs"

try {
    if ($Example -notmatch '^[a-z0-9][a-z0-9-]*$' `
        -or $Scenario -notmatch '^[a-z0-9][a-z0-9-]*$') {
        Fail-Stage "arguments" "usage: run-scenario.ps1 <example> <scenario> [--update-golden]"
    }
    if (-not (Test-Path -LiteralPath $Registry -PathType Leaf) `
        -or -not ([System.IO.File]::ReadAllLines($Registry) -contains "$Example $Scenario")) {
        Fail-Stage "arguments" "scenario '$Example/$Scenario' is not registered"
    }
    if (-not $Vehicles.ContainsKey($Example)) {
        Fail-Stage "arguments" "the Win32 runner has no vehicle for '$Example'"
    }
    # Named before the vehicle starts. The name is not a fact about the run, so
    # nothing has to be captured first, and an undeclared machine is turned away
    # without opening a window.
    $RigDescriptor = Resolve-RigDescriptorPath (Get-RigDescriptorDirectory) $env:LOKA_WIN32_RIG
    $RigName = [System.IO.Path]::GetFileNameWithoutExtension($RigDescriptor)
    $GoldenRoot = Join-Path $ProjectDirectory "build/win32-scenario/golden/$RigName"
    $GoldenDirectory = Join-Path $GoldenRoot $Example
    $Golden = Join-Path $GoldenDirectory "$Scenario.png"
    $GoldenProfile = Join-Path $GoldenDirectory "$Scenario.profile"
    $Vehicle = $Vehicles[$Example]
    $VehicleExecutableName = $Vehicle.Executable
    $VehicleOutputDirectory = $Vehicle.OutputDirectory
    $BuiltExecutable = Join-Path $ProjectDirectory `
        "build/win32/Debug/example/$VehicleOutputDirectory/$VehicleExecutableName"
    if (-not (Test-Path -LiteralPath $BuiltExecutable -PathType Leaf)) {
        Fail-Stage "build" "missing $BuiltExecutable; enter vcvarsall.bat for this machine's architecture, run cmake --preset win32-debug, then cmake --build --preset win32-tests"
    }
    if ($Example -eq "scrapbook") {
        if (-not (Test-Path -LiteralPath $Lrpc -PathType Leaf)) {
            if (Test-Path -LiteralPath $MultiConfigLrpc -PathType Leaf) {
                $Lrpc = $MultiConfigLrpc
            } else {
                Fail-Stage "build" "missing native lrpc.exe under build/host/lrpc; run cmake -S tools/lrpc -B build/host/lrpc and cmake --build build/host/lrpc"
            }
        }
        if (-not (Test-Path -LiteralPath $SourceAssets -PathType Leaf)) {
            Fail-Stage "stage" "missing $SourceAssets"
        }
    }
    if (-not (Test-Path -LiteralPath $ExpectedAudit -PathType Leaf)) {
        Fail-Stage "verdict" "missing tracked audit $ExpectedAudit"
    }
    Resolve-Python

    if (Test-Path -LiteralPath $Work) {
        Remove-Item -LiteralPath $Work -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Stage, $Settle -Force | Out-Null
    [System.IO.File]::WriteAllText($RunnerLog, "", (New-Object System.Text.UTF8Encoding($false)))
    Write-Runner "stage: build artifact $BuiltExecutable"
    Write-Runner "stage: native directory (no development disk)"

    $StagedExecutable = Join-Path $Stage $VehicleExecutableName
    Copy-Item -LiteralPath $BuiltExecutable -Destination $StagedExecutable
    if ($Example -eq "scrapbook") {
        $StagedAssets = Join-Path $Stage "ASSETS.LRP"
        $stageArguments = @("stage", $SourceAssets, "-o", $StagedAssets)
        $corruptBag = ((Invoke-PackageFixturePlan) | Out-String).Trim()
        if ($PackageFixtureExitCode -ne 0) {
            Fail-Stage "stage" $corruptBag
        }
        # The refusal message is worth merging into the value, but only on
        # failure: on success anything the interpreter wrote to stderr rides
        # along. On this rail that interpreter can be `wsl.exe python3`, whose
        # interop warnings land on stderr, so the answer shape is checked
        # rather than trusted. Empty or decimal, nothing else.
        if ($corruptBag.Length -ne 0) {
            if ($corruptBag -notmatch '^[0-9]+$') {
                Fail-Stage "stage" "package fixture plan answered '$corruptBag', not a bag number"
            }
            $stageArguments += @("--corrupt-bag", $corruptBag)
        }
        & $Lrpc @stageArguments *>> $RunnerLog
        if ($LASTEXITCODE -ne 0) {
            Fail-Stage "stage" "lrpc refused the staged package; see $RunnerLog"
        }
        $fixtureMessage = ((Invoke-PackageFixtureVerify $StagedAssets) | Out-String).Trim()
        if ($PackageFixtureExitCode -ne 0) {
            Fail-Stage "stage" $fixtureMessage
        }
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $Stage "LokaTest.cfg"),
        "scenario $Scenario`ncapture_dir .`nlinger_seconds 120`n",
        (New-Object System.Text.UTF8Encoding($false)))

    Add-Type -AssemblyName System.Drawing
    Add-Type -Path $NativeSource -ReferencedAssemblies System.Drawing
    $CaptureDpiSetup = [LokaScenarioNative]::PrepareCaptureProcess()
    Write-Runner "stage: capture DPI setup $CaptureDpiSetup"

    Write-Runner "stage: launch"
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $StagedExecutable
    $startInfo.WorkingDirectory = $Stage
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $false
    $launchProcess = New-Object System.Diagnostics.Process
    $launchProcess.StartInfo = $startInfo
    if (-not $launchProcess.Start()) {
        Fail-Stage "launch" "could not start $StagedExecutable"
    }
    # The cleanup owner sees only a process that successfully acquired a native handle.
    $ChildProcess = $launchProcess
    $ChildStdoutTask = $ChildProcess.StandardOutput.ReadToEndAsync()
    $ChildStderrTask = $ChildProcess.StandardError.ReadToEndAsync()
    [System.IO.File]::WriteAllText((Join-Path $Work "child.pid"), [string]$ChildProcess.Id)

    $Completion = Join-Path $Stage "LokaTestsWin32.complete"
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while (-not (Test-Path -LiteralPath $Completion -PathType Leaf)) {
        $ChildProcess.Refresh()
        if ($ChildProcess.HasExited) {
            Fail-Stage "drive" "scenario process exited $($ChildProcess.ExitCode) before completion; see child.stderr.log"
        }
        if ([DateTime]::UtcNow -ge $deadline) {
            Fail-Stage "drive" "timed out waiting for the completion marker"
        }
        Start-Sleep -Milliseconds 100
    }
    if ([System.IO.File]::ReadAllText($Completion).Trim() -ne "audit-ready") {
        Fail-Stage "completion" "atomic completion marker is invalid"
    }

    $StageAudit = Join-Path $Stage "actual.audit"
    $ActualAudit = Join-Path $Work "actual.audit"
    if (-not (Test-Path -LiteralPath $StageAudit -PathType Leaf)) {
        Fail-Stage "extract" "scenario process did not write actual.audit"
    }
    Copy-Item -LiteralPath $StageAudit -Destination $ActualAudit
    if (-not (Test-FilesEqual $ExpectedAudit $ActualAudit)) {
        Fail-Stage "verdict" "actual.audit differs byte-for-byte from $ExpectedAudit"
    }
    $expectedHash = (Get-FileHash -LiteralPath $ExpectedAudit -Algorithm SHA256).Hash.ToLowerInvariant()
    $actualHash = (Get-FileHash -LiteralPath $ActualAudit -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Runner "audit verdict: byte-identical expected_sha256=$expectedHash actual_sha256=$actualHash"

    $windows = [LokaScenarioNative]::VisibleTopLevelWindows([uint32]$ChildProcess.Id)
    if ($windows.Count -ne 1) {
        Fail-Stage "capture" "expected one visible top-level HWND for PID $($ChildProcess.Id), found $($windows.Count)"
    }
    $ChildWindow = $windows[0]
    Write-Runner ("stage: capture PID={0} HWND=0x{1:X}" -f $ChildProcess.Id, $ChildWindow.ToInt64())

    $previousHash = $null
    $acceptedFrame = $null
    $settleDeadline = [DateTime]::UtcNow.AddSeconds(30)
    for ($frame = 1; [DateTime]::UtcNow -lt $settleDeadline; ++$frame) {
        $framePath = Join-Path $Settle ("frame-{0:D3}.png" -f $frame)
        $captureResult = [LokaScenarioNative]::CaptureWindow(
            $ChildWindow, $framePath, $CaptureDpiSetup)
        if ($captureResult -eq [LokaScenarioCaptureResult]::PhysicalBoundsUnavailable) {
            Fail-Stage "capture" "no DPI-awareness capability can provide physical window bounds"
        }
        if ($captureResult -eq [LokaScenarioCaptureResult]::Refused) {
            Fail-Stage "capture" "PrintWindow(PW_RENDERFULLCONTENT) refused frame $frame"
        }
        if ($captureResult -eq [LokaScenarioCaptureResult]::Uniform) {
            Write-Runner "settle frame $frame is uniform; retrying until the settle deadline"
            $previousHash = $null
            Start-Sleep -Milliseconds 250
            continue
        }
        $hash = (Get-FileHash -LiteralPath $framePath -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Runner "settle frame $frame sha256=$hash"
        if ($null -ne $previousHash -and $hash -eq $previousHash) {
            $acceptedFrame = $framePath
            break
        }
        $previousHash = $hash
        Start-Sleep -Milliseconds 250
    }
    if ($null -eq $acceptedFrame) {
        Fail-Stage "settle" "no two consecutive non-uniform screenshots had identical hashes before the deadline"
    }
    $WindowActual = Join-Path $Work "window-actual.png"
    Copy-Item -LiteralPath $acceptedFrame -Destination $WindowActual

    if (-not [LokaScenarioNative]::CloseWindow($ChildWindow)) {
        Fail-Stage "shutdown" "WM_CLOSE could not be posted to the owned HWND"
    }
    if (-not $ChildProcess.WaitForExit(10000)) {
        Fail-Stage "shutdown" "scenario process did not exit after WM_CLOSE"
    }
    # Complete the redirected stdout/stderr drains before observing ExitCode;
    # the timed overload alone can leave the PowerShell Process wrapper's
    # terminal properties unmaterialized even after the native handle signaled.
    $ChildProcess.WaitForExit()
    $ChildProcess.Refresh()
    $childExitCode = $ChildProcess.ExitCode
    [System.IO.File]::WriteAllText((Join-Path $Work "child.stdout.log"), $ChildStdoutTask.Result)
    [System.IO.File]::WriteAllText((Join-Path $Work "child.stderr.log"), $ChildStderrTask.Result)
    [System.IO.File]::WriteAllText((Join-Path $Work "child-exit.txt"), [string]$childExitCode)
    if ($childExitCode -ne 0) {
        Fail-Stage "shutdown" "scenario process exited $childExitCode"
    }

    $StageBounds = Join-Path $Stage "capture.bounds"
    $StageProfile = Join-Path $Stage "actual.profile"
    if (-not (Test-Path -LiteralPath $StageBounds -PathType Leaf) `
        -or -not (Test-Path -LiteralPath $StageProfile -PathType Leaf)) {
        Fail-Stage "artifacts" "scenario process omitted capture.bounds or actual.profile"
    }
    $Bounds = Read-CaptureBounds $StageBounds
    $Actual = Join-Path $Work "actual.png"
    Invoke-PngCrop $WindowActual `
        $Bounds["left"] $Bounds["top"] $Bounds["right"] $Bounds["bottom"] $Actual *>> $RunnerLog
    if ($PngExitCode -ne 0) {
        Fail-Stage "crop" "content bounds do not fit the captured window; see $RunnerLog"
    }
    $ActualProfile = Join-Path $Work "actual.profile"
    Copy-Item -LiteralPath $StageProfile -Destination $ActualProfile

    # Before the golden is written or compared, not after. A bake on a machine
    # that does not match the tracked rig would otherwise pin whatever the
    # desktop happened to be, and the disagreement would surface as a pixel
    # mismatch on some later, unrelated change.
    Invoke-CaptureProfileGuard $RigDescriptor $ActualProfile *>> $RunnerLog
    if ($CaptureProfileExitCode -ne 0) {
        Fail-Stage "profile" "capture environment is not the one $RigDescriptor declares; see $RunnerLog"
    }

    if ($UpdateGolden) {
        Invoke-GoldenIdentityGuard $Actual *>> $RunnerLog
        if ($GoldenIdentityExitCode -ne 0) {
            Fail-Stage "golden" "settled capture failed the startup-identity contract; see $RunnerLog"
        }
        New-Item -ItemType Directory -Path $GoldenDirectory -Force | Out-Null
        Copy-Item -LiteralPath $Actual -Destination $Golden
        Copy-Item -LiteralPath $ActualProfile -Destination $GoldenProfile
        Write-Runner "Updated Win32 rig golden: $Golden"
        Write-Runner "Reminder: attach visual evidence to the PR; the rig-local golden is untracked."
        $ExitCode = 0
    } else {
        if (-not (Test-Path -LiteralPath $Golden -PathType Leaf) `
            -or -not (Test-Path -LiteralPath $GoldenProfile -PathType Leaf)) {
            Fail-Stage "golden" "missing rig-local golden/profile; rerun with --update-golden"
        }
        $WorkGolden = Join-Path $Work "golden.png"
        Copy-Item -LiteralPath $Golden -Destination $WorkGolden
        $expectedProfile = Read-ScenarioProfile $GoldenProfile
        $actualProfileFacts = Read-ScenarioProfile $ActualProfile
        $profileMismatch = Get-CaptureProfileMismatch $expectedProfile $actualProfileFacts
        if ($null -ne $profileMismatch) {
            $expectedValue = if ($expectedProfile.ContainsKey($profileMismatch)) {
                $expectedProfile[$profileMismatch]
            } else {
                "<absent>"
            }
            $actualValue = if ($actualProfileFacts.ContainsKey($profileMismatch)) {
                $actualProfileFacts[$profileMismatch]
            } else {
                "<absent>"
            }
            Fail-Stage "profile" "capture field '$profileMismatch' moved from '$expectedValue' to '$actualValue'"
        }
        # TODO(#459): tolerate at most 400 differing pixels in at most two
        # destination x columns. Two is the tracked one-or-two full-height-column
        # wobble: 169 px each and 338 px worst observed. A missing glyph spans
        # roughly six to eight columns and an icon many more, so the shape bound
        # still rejects content loss.
        # Remove when the compositing M1 offscreen composition lands.
        Invoke-PngCompare $Actual $WorkGolden 400 2 *>> $RunnerLog
        $CompareExitCode = $PngExitCode
        if ($null -eq $PngCompareResult) {
            Fail-Stage "golden" "pngtool compare omitted its measured diff counts; see $RunnerLog"
        }
        if ($PngCompareResult.DifferenceCount -gt 0) {
            $Diff = Join-Path $Work "diff.png"
            Invoke-PngDiff $WorkGolden $Actual $Diff *>> $RunnerLog
            if (-not (Test-Path -LiteralPath $Diff -PathType Leaf)) {
                Fail-Stage "golden" "pixel mismatch was detected but diff.png could not be written"
            }
        }
        if ($CompareExitCode -ne 0) {
            Fail-Stage "golden" "actual pixels differ from $Golden; see actual.png, golden.png, and diff.png"
        }
        [System.IO.File]::WriteAllText((Join-Path $Work "verified"), "runner-verified`n")
        Write-Runner "Scenario passed: $Example/$Scenario"
        $ExitCode = 0
    }
} catch {
    $ExitCode = 1
    [Console]::Error.WriteLine($_.Exception.Message)
    [Console]::Error.WriteLine("Work directory left for inspection: $Work")
} finally {
    if ($null -ne $ChildProcess) {
        $ChildProcess.Refresh()
        if (-not $ChildProcess.HasExited) {
            if ($ChildWindow -ne [IntPtr]::Zero) {
                [void][LokaScenarioNative]::CloseWindow($ChildWindow)
                [void]$ChildProcess.WaitForExit(3000)
                $ChildProcess.Refresh()
            }
            if (-not $ChildProcess.HasExited) {
                Stop-Process -Id $ChildProcess.Id -Force -ErrorAction SilentlyContinue
                [void]$ChildProcess.WaitForExit(3000)
            }
        }
        if ((Test-Path -LiteralPath $Work) -and -not (Test-Path -LiteralPath (Join-Path $Work "child-exit.txt"))) {
            $childResult = if ($ChildProcess.HasExited) {
                [string]$ChildProcess.ExitCode
            } else {
                "forced-timeout"
            }
            [System.IO.File]::WriteAllText((Join-Path $Work "child-exit.txt"), $childResult)
        }
        if ($null -ne $ChildStdoutTask -and $ChildStdoutTask.IsCompleted) {
            [System.IO.File]::WriteAllText((Join-Path $Work "child.stdout.log"), $ChildStdoutTask.Result)
        }
        if ($null -ne $ChildStderrTask -and $ChildStderrTask.IsCompleted) {
            [System.IO.File]::WriteAllText((Join-Path $Work "child.stderr.log"), $ChildStderrTask.Result)
        }
    }
}

exit $ExitCode

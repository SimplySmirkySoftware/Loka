$ErrorActionPreference = "Stop"
# Windows PowerShell 4.0 exits 0 from a -File script that throws; see the trap
# in scripts/win32-standalone-flow.ps1.
trap {
    [Console]::Error.WriteLine($_)
    exit 1
}

& (Join-Path $PSScriptRoot "Win32StandaloneProfileTest.ps1")

$ProjectDirectory = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Subject = Join-Path $ProjectDirectory "scripts/win32-standalone-flow.ps1"
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("loka-win32-stage-" + [Guid]::NewGuid())
$BuildRoot = Join-Path $TestRoot "build"
$StageRoot = Join-Path $TestRoot "completed stage"
$ToolRoot = Join-Path $TestRoot "tools"
$Catalog = @(
    @("scrapbook", "LokaScrapbookStandaloneFlowWin32", "scrapbook/standalone-tour.audit"),
    @("helloworld", "LokaHelloWorldStandaloneFlowWin32", "helloworld/toggle-action-probe.audit"),
    @("tutorial", "LokaTutorialStandaloneFlowWin32", "tutorial/increment-summary-toggle.audit"),
    @("minesweeper", "LokaMineSweeperStandaloneFlowWin32", "minesweeper/new-game-twice.audit"),
    @("floppybird", "LokaFloppyBirdStandaloneFlowWin32", "floppybird/fixed-step-flaps.audit")
)

function New-TestPe([string]$Path, [byte]$Marker) {
    $bytes = New-Object byte[] 512
    $bytes[0] = 0x4D
    $bytes[1] = 0x5A
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3C)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($bytes, 0x84)
    $bytes[0x100] = $Marker
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Assert-BytesEqual([string]$Expected, [string]$Actual, [string]$Message) {
    $expectedBytes = [System.IO.File]::ReadAllBytes($Expected)
    $actualBytes = [System.IO.File]::ReadAllBytes($Actual)
    if ($expectedBytes.Length -ne $actualBytes.Length) {
        throw $Message
    }
    for ($index = 0; $index -lt $expectedBytes.Length; ++$index) {
        if ($expectedBytes[$index] -ne $actualBytes[$index]) {
            throw $Message
        }
    }
}

function Invoke-Stage([string]$Action = "Stage") {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Subject `
        -Action $Action -Architecture x64 `
        -BuildDirectory $BuildRoot -StageDirectory $StageRoot *> $null
    return $LASTEXITCODE
}

try {
    $builtDirectory = Join-Path $BuildRoot "standalone-flow"
    New-Item -ItemType Directory -Path `
        $builtDirectory, $StageRoot, $ToolRoot -Force | Out-Null

    $catalogLines = @(
        "# SimpleViewer is excluded from automation: interactive file chooser."
    )
    for ($index = 0; $index -lt $Catalog.Count; ++$index) {
        $entry = $Catalog[$index]
        $appDirectory = Join-Path $builtDirectory $entry[0]
        $loopDirectory = Join-Path $BuildRoot ("standalone-loop/" + $entry[0])
        New-Item -ItemType Directory -Path $appDirectory, $loopDirectory | Out-Null
        New-TestPe (Join-Path $loopDirectory ($entry[1].Replace("StandaloneFlow", "StandaloneLoop") + ".exe")) `
            ([byte](0x40 + $index))
        New-TestPe (Join-Path $appDirectory ($entry[1] + ".exe")) `
            ([byte](0x20 + $index))
        $catalogLines += ($entry[0] + "`t" + $entry[1] + "`t" +
            "tests/scenarios/expected/" + $entry[2])
    }
    $buildCatalog = Join-Path $BuildRoot "standalone-flow-catalog.tsv"
    [System.IO.File]::WriteAllLines($buildCatalog, $catalogLines)
    $viewerDirectory = Join-Path $BuildRoot "example/SimpleViewer"
    New-Item -ItemType Directory -Path $viewerDirectory | Out-Null
    $viewerExecutable = Join-Path $viewerDirectory "LokaSimpleViewerWin32.exe"
    New-TestPe $viewerExecutable 0x60
    $textDirectory = Join-Path $BuildRoot "example/SimpleText"
    New-Item -ItemType Directory -Path $textDirectory | Out-Null
    $textExecutable = Join-Path $textDirectory "LokaSimpleTextWin32.exe"
    New-TestPe $textExecutable 0x61
    $smirkyDirectory = Join-Path $BuildRoot "example/SmirkyCard"
    New-Item -ItemType Directory -Path $smirkyDirectory | Out-Null
    $smirkyExecutable = Join-Path $smirkyDirectory "LokaSmirkyCardWin32.exe"
    New-TestPe $smirkyExecutable 0x62
    $smirkyScripts = @([System.IO.File]::ReadAllLines(
            (Join-Path $ProjectDirectory "example/SmirkyCard/disk-scripts.txt")) |
        ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith("#") })
    $smirkyScripts += @([System.IO.File]::ReadAllLines(
            (Join-Path $ProjectDirectory "example/SmirkyCard/disk-pictures.txt")) |
        ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith("#") } |
        ForEach-Object { "Pictures/" + $_ + ".PNG" })
    New-Item -ItemType Directory -Force -Path (Join-Path $smirkyDirectory "Pictures") | Out-Null
    foreach ($name in $smirkyScripts) {
        [System.IO.File]::WriteAllText((Join-Path $smirkyDirectory $name), "// $name")
    }
    $loopAssets = Join-Path $BuildRoot "standalone-loop/scrapbook/ASSETS.LRP"
    [System.IO.File]::WriteAllText($loopAssets, "loop assets")
    $builtAssets = Join-Path $builtDirectory "scrapbook/ASSETS.LRP"
    [System.IO.File]::WriteAllText($builtAssets, "new assets")
    $sentinel = Join-Path $StageRoot "completed-stage-sentinel.txt"
    [System.IO.File]::WriteAllText($sentinel, "old completed stage")

    Set-Content -LiteralPath (Join-Path $ToolRoot "cmake.cmd") -Value "@exit /b 0"
    Copy-Item -LiteralPath (Join-Path $env:SystemRoot "System32/where.exe") `
        -Destination (Join-Path $ToolRoot "cl.exe")

    $previousPath = $env:PATH
    $previousTarget = $env:VSCMD_ARG_TGT_ARCH
    $env:PATH = "$ToolRoot;$previousPath"
    $env:VSCMD_ARG_TGT_ARCH = "x64"
    $lockedAssets = [System.IO.File]::Open(
        $builtAssets,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::None)
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $stageExitCode = Invoke-Stage
    } finally {
        $ErrorActionPreference = $previousErrorAction
        $lockedAssets.Dispose()
    }

    if ($stageExitCode -eq 0) {
        throw "Stage unexpectedly succeeded while its assets source was locked."
    }
    if (-not (Test-Path -LiteralPath $sentinel) `
        -or [System.IO.File]::ReadAllText($sentinel) -ne "old completed stage") {
        throw "A failed Stage replaced the prior completed directory."
    }
    $transactionResidue = @(Get-ChildItem -LiteralPath $TestRoot -Force | Where-Object {
        $_.Name -like ".completed stage.*"
    })
    if ($transactionResidue.Count -ne 0) {
        throw "Failed Stage left transaction directories behind."
    }

    $stageExitCode = Invoke-Stage
    if ($stageExitCode -ne 0) {
        throw "Completed Stage failed with exit code $stageExitCode."
    }
    if (Test-Path -LiteralPath $sentinel) {
        throw "Completed Stage retained a file from the replaced directory."
    }

    foreach ($entry in $Catalog) {
        Assert-BytesEqual `
            (Join-Path $builtDirectory ($entry[0] + "/" + $entry[1] + ".exe")) `
            (Join-Path $StageRoot ($entry[0] + "/" + $entry[1] + ".exe")) `
            "Completed Stage did not install $($entry[0])."
        Assert-BytesEqual `
            (Join-Path $ProjectDirectory ("tests/scenarios/expected/" + $entry[2])) `
            (Join-Path $StageRoot ("expected/" + $entry[0] + ".audit")) `
            "Completed Stage did not install the $($entry[0]) expected audit."
    }
    Assert-BytesEqual $builtAssets (Join-Path $StageRoot "scrapbook/ASSETS.LRP") `
        "Completed Stage did not install the built assets."
    Assert-BytesEqual $buildCatalog `
        (Join-Path $StageRoot "standalone-flow-catalog.tsv") `
        "Completed Stage did not install the generated catalog."
    Assert-BytesEqual $Subject (Join-Path $StageRoot "Verify-StandaloneFlow.ps1") `
        "Completed Stage did not install its verifier."

    $transactionResidue = @(Get-ChildItem -LiteralPath $TestRoot -Force | Where-Object {
        $_.Name -like ".completed stage.*"
    })
    if ($transactionResidue.Count -ne 0) {
        throw "Completed Stage left transaction directories behind."
    }

    if (Test-Path -LiteralPath (Join-Path $StageRoot "ASSETS.LRP")) {
        throw "Stage left shared assets beside the application directories."
    }
    foreach ($entry in $Catalog) {
        if (Test-Path -LiteralPath (Join-Path $StageRoot ($entry[1] + ".exe"))) {
            throw "Stage left a flat executable for $($entry[0])."
        }
    }

    # Release replaces the finite package atomically and keeps each interactive app
    # in its own folder alongside the five autonomous application folders.
    $lockedAssets = [System.IO.File]::Open(
        $loopAssets, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::None)
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $releaseExitCode = Invoke-Stage "Release"
    } finally {
        $ErrorActionPreference = $previousErrorAction
        $lockedAssets.Dispose()
    }
    if ($releaseExitCode -eq 0) {
        throw "Release unexpectedly succeeded with locked assets."
    }
    Assert-BytesEqual $Subject (Join-Path $StageRoot "Verify-StandaloneFlow.ps1") `
        "Failed Release destroyed the prior completed Stage."

    $releaseExitCode = Invoke-Stage "Release"
    if ($releaseExitCode -ne 0) {
        throw "Release failed with exit code $releaseExitCode."
    }
    foreach ($entry in $Catalog) {
        $loopName = $entry[1].Replace("StandaloneFlow", "StandaloneLoop") + ".exe"
        Assert-BytesEqual `
            (Join-Path $BuildRoot ("standalone-loop/" + $entry[0] + "/" + $loopName)) `
            (Join-Path $StageRoot ($entry[0] + "/" + $loopName)) `
            "Release did not install the $($entry[0]) loop."
    }
    Assert-BytesEqual $loopAssets (Join-Path $StageRoot "scrapbook/ASSETS.LRP") `
        "Release did not install Scrapbook's own assets."
    Assert-BytesEqual $viewerExecutable `
        (Join-Path $StageRoot "simpleviewer/LokaSimpleViewerWin32.exe") `
        "Release did not install SimpleViewer in its own directory."
    Assert-BytesEqual $textExecutable `
        (Join-Path $StageRoot "simpletext/LokaSimpleTextWin32.exe") `
        "Release did not install SimpleText in its own directory."
    Assert-BytesEqual $smirkyExecutable `
        (Join-Path $StageRoot "smirkycard/LokaSmirkyCardWin32.exe") `
        "Release did not install SmirkyCard in its own directory."
    foreach ($name in $smirkyScripts) {
        Assert-BytesEqual (Join-Path $smirkyDirectory $name) `
            (Join-Path $StageRoot ("smirkycard/" + $name)) `
            "Release did not install SmirkyCard's $name beside it."
    }
    $rootFiles = @(Get-ChildItem -LiteralPath $StageRoot -File)
    if ($rootFiles.Count -ne 1 -or $rootFiles[0].Name -ne "README.txt") {
        throw "Release retained flat executables, shared sidecars, or stale Stage files."
    }
    $folders = @(Get-ChildItem -LiteralPath $StageRoot -Directory)
    if ($folders.Count -ne 8) {
        throw "Release must contain one directory per application."
    }
    $transactionResidue = @(Get-ChildItem -LiteralPath $TestRoot -Force | Where-Object {
        $_.Name -like ".completed stage.*"
    })
    if ($transactionResidue.Count -ne 0) {
        throw "Release left transaction directories behind."
    }
    Write-Output "Win32 Stage and Release isolate application files and publish atomically."
} finally {
    if ($null -ne $previousPath) { $env:PATH = $previousPath }
    if ($null -ne $previousTarget) { $env:VSCMD_ARG_TGT_ARCH = $previousTarget }
    Remove-Item -LiteralPath $TestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Key,
    [switch]$Target,
    [switch]$Build,
    [switch]$BuildAndPrepare
)

$ErrorActionPreference = "Stop"
$workspace = Split-Path -Parent $PSScriptRoot
# Matches mame-dev-disk-app.sh; a missing selection is invalid in VS Code.
$selectedPreset = if (Test-Path Env:LOKA_RETRO68_SELECTED_PRESET) { $env:LOKA_RETRO68_SELECTED_PRESET } else { "retro68-68k-release" }
if ($selectedPreset -like "retro68-68k-*") {
    $cpu = "68k"; $suffix = "68K"
} elseif ($selectedPreset -like "retro68-ppc-*") {
    $cpu = "ppc"; $suffix = "PPC"
} else {
    throw "Select a Retro68 68K or PPC configure preset in CMake Tools."
}
$env:LOKA_MAME_CPU = $cpu
$preset = "retro68-${cpu}-release"
$data = $null

switch ($Key) {
    { $_ -in @("AllStandaloneLoops", "AllStandaloneFlows") } {
        if ($Target) { throw "$Key has no single application target" }
        $preset = "retro68-${cpu}-standalone-release"
        if ($Key -eq "AllStandaloneLoops") {
            $batchTarget = "LokaStandaloneLoop${suffix}All"
            $diskOption = "--all-loops"
        } else {
            $batchTarget = "LokaStandaloneFlow${suffix}All"
            $diskOption = "--all-flows"
        }
        if ($Build -or $BuildAndPrepare) {
            & bash "$PSScriptRoot/retro68-cmake.sh" --preset $preset
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            & bash "$PSScriptRoot/retro68-cmake.sh" --build --preset $preset --target $batchTarget
            if ($LASTEXITCODE -ne 0 -or $Build) { exit $LASTEXITCODE }
        }
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/mame-dev-disk.ps1" -MacBinaryPath $diskOption
        exit $LASTEXITCODE
    }
    "AllLoops" {
        if (-not $Build -or $Target -or $BuildAndPrepare) {
            throw "AllLoops is available only with -Build"
        }
        foreach ($loop in @("ScrapbookStandaloneLoop", "HelloWorldStandaloneLoop", "TutorialStandaloneLoop", "MineSweeperStandaloneLoop", "FloppyBirdStandaloneLoop", "ScrapbookStandaloneFlow", "HelloWorldStandaloneFlow", "TutorialStandaloneFlow", "MineSweeperStandaloneFlow", "FloppyBirdStandaloneFlow", "HelloWorldScenarioLoop", "MineSweeperScenarioLoop")) {
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath $loop -Build
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        }
        exit 0
    }
    "ScrapbookStandaloneLoop" { $cmakeTarget = "LokaScrapbookStandaloneLoop${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaScrapbookStandaloneLoop${suffix}.bin"; $data = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/ASSETS.LRP" }
    "HelloWorldStandaloneLoop" { $cmakeTarget = "LokaHelloStandaloneLoop${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaHelloStandaloneLoop${suffix}.bin" }
    "TutorialStandaloneLoop" { $cmakeTarget = "LokaTutorialStandaloneLoop${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaTutorialStandaloneLoop${suffix}.bin" }
    "MineSweeperStandaloneLoop" { $cmakeTarget = "LokaMineStandaloneLoop${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaMineStandaloneLoop${suffix}.bin" }
    "FloppyBirdStandaloneLoop" { $cmakeTarget = "LokaFloppyStandaloneLoop${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaFloppyStandaloneLoop${suffix}.bin" }
    "All" {
        if ($Target) { throw "All has no single target" }
        if ($Build -or $BuildAndPrepare) {
            $configureArguments = @("--preset", $preset)
            if ($BuildAndPrepare) { $configureArguments += "-DLOKA_BUILD_SMIRKYCARD=ON" }
            & bash "$PSScriptRoot/retro68-cmake.sh" @configureArguments
            if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
            & bash "$PSScriptRoot/retro68-cmake.sh" --build --preset $preset
            if ($LASTEXITCODE -ne 0 -or $Build) { exit $LASTEXITCODE }
        }
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/mame-dev-disk.ps1" -MacBinaryPath "--all"
        exit $LASTEXITCODE
    }
    "HelloWorld" { $cmakeTarget = "LokaHello${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/HelloWorld/LokaHello${suffix}.bin" }
    "SmirkyCard" {
        $cmakeTarget = "LokaSmirkyCard${suffix}_APPL"
        $bin = "build/retro68/${cpu}/Release/example/SmirkyCard/LokaSmirkyCard${suffix}.bin"
        # Manifest readers in mame-*-disk* and SmirkyCard/CMakeLists.txt stay parallel.
        $data = @(
            foreach ($line in Get-Content -LiteralPath (Join-Path $workspace "example/SmirkyCard/disk-scripts.txt")) {
                $script = $line.Trim()
                if (!$script -or $script.StartsWith("#")) { continue }
                "example/SmirkyCard/$script"
            }
            # The disk writer stages these canonical asset paths in Pictures/.
            foreach ($line in Get-Content -LiteralPath (Join-Path $workspace "example/SmirkyCard/disk-pictures.txt")) {
                $script = $line.Trim()
                if (!$script -or $script.StartsWith("#")) { continue }
                "example/SmirkyCard/assets/$script.PICT"
            }
        )
    }
    "MineSweeper" { $cmakeTarget = "LokaMine${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/MineSweeper/LokaMine${suffix}.bin" }
    "SimpleViewer" { $cmakeTarget = "LokaSimpleViewer${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/SimpleViewer/LokaSimpleViewer${suffix}.bin" }
    "SimpleText" { $cmakeTarget = "LokaSimpleText${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/SimpleText/LokaSimpleText${suffix}.bin" }
    "FloppyBird" { $cmakeTarget = "LokaFloppyBird${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/FloppyBird/LokaFloppyBird${suffix}.bin" }
    "SmirkBench" { $cmakeTarget = "LokaSmirkBench${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/SmirkBench/LokaSmirkBench${suffix}.bin" }
    "LazyList" { $cmakeTarget = "LokaLazyList${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/LazyList/LokaLazyList${suffix}.bin" }
    "Tutorial" { $cmakeTarget = "LokaTutorial${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/Tutorial/LokaTutorial${suffix}.bin" }
    "ScrapbookUI" { $cmakeTarget = "ScrapbookUI${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/example/ScrapbookUI/ScrapbookUI${suffix}.bin"; $data = "build/retro68/${cpu}/Release/example/ScrapbookUI/ASSETS.LRP" }
    "ScrapbookStandaloneFlow" { $cmakeTarget = "LokaScrapbookStandaloneFlow${suffix}_APPL"; $bin = "build/presentation/toolbox-${cpu}-release/LokaScrapbookStandaloneFlow${suffix}.bin"; $data = "build/presentation/toolbox-${cpu}-release/ASSETS.LRP" }
    "HelloWorldStandaloneFlow" { $cmakeTarget = "LokaHelloStandaloneFlow${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaHelloStandaloneFlow${suffix}.bin" }
    "TutorialStandaloneFlow" { $cmakeTarget = "LokaTutorialStandaloneFlow${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaTutorialStandaloneFlow${suffix}.bin" }
    "MineSweeperStandaloneFlow" { $cmakeTarget = "LokaMineStandaloneFlow${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaMineStandaloneFlow${suffix}.bin" }
    "FloppyBirdStandaloneFlow" { $cmakeTarget = "LokaFloppyStandaloneFlow${suffix}_APPL"; $preset = "retro68-${cpu}-standalone-release"; $bin = "build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaFloppyStandaloneFlow${suffix}.bin" }
    "HelloWorldScenarioLoop" { $cmakeTarget = "LokaHelloScenarioLoop${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/tests/toolbox/LokaHelloScenarioLoop${suffix}.bin" }
    "MineSweeperScenarioLoop" { $cmakeTarget = "LokaMineScenarioLoop${suffix}_APPL"; $bin = "build/retro68/${cpu}/Release/tests/toolbox/LokaMineScenarioLoop${suffix}.bin" }
    default { throw "unknown SCSI app key: $Key" }
}

function Build-App {
    if ($Key -eq "ScrapbookStandaloneFlow") {
        & bash "$PSScriptRoot/toolbox-standalone-flow.sh" Stage $cpu
    } else {
        if ($Key -eq "SmirkyCard") {
            & bash "$PSScriptRoot/retro68-cmake.sh" --preset $preset -DLOKA_BUILD_SMIRKYCARD=ON
        } else {
            & bash "$PSScriptRoot/retro68-cmake.sh" --preset $preset
        }
        if ($LASTEXITCODE -eq 0) {
            & bash "$PSScriptRoot/retro68-cmake.sh" --build --preset $preset --target $cmakeTarget
        }
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Target) {
    Write-Output $cmakeTarget
} elseif ($Build) {
    Build-App
} else {
    if ($BuildAndPrepare) { Build-App }
    $arguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "$PSScriptRoot/mame-dev-disk.ps1", (Join-Path $workspace $bin))
    foreach ($item in $data) { $arguments += (Join-Path $workspace $item) }
    & powershell.exe @arguments
    exit $LASTEXITCODE
}

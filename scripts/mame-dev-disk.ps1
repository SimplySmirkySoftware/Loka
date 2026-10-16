param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$MacBinaryPath,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$PlainDataPaths = @()
)

$ErrorActionPreference = "Stop"

$ScriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDirectory = Split-Path -Parent $ScriptDirectory
$EnvironmentFile = if ($env:MAME_ENV_FILE) {
    $env:MAME_ENV_FILE
} else {
    Join-Path $ProjectDirectory ".env-mame"
}

if (Test-Path -LiteralPath $EnvironmentFile) {
    foreach ($line in Get-Content -LiteralPath $EnvironmentFile) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith("#")) {
            continue
        }
        if ($trimmed -notmatch "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$") {
            throw "Invalid environment line in ${EnvironmentFile}: $line"
        }
        $name = $Matches[1]
        $value = $Matches[2].Trim()
        if (($value.StartsWith('"') -and $value.EndsWith('"')) -or
            ($value.StartsWith("'") -and $value.EndsWith("'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $value = $value.Replace('${HOME}', $HOME).Replace('$HOME', $HOME)
        $value = [Environment]::ExpandEnvironmentVariables($value)
        Set-Item -Path "Env:$name" -Value $value
    }
}

function Find-Retro68Tool([string]$Name) {
    $executableName = if ($IsWindows -or $env:OS -eq "Windows_NT") {
        "$Name.exe"
    } else {
        $Name
    }
    $candidates = @()
    if ($env:RETRO68_TOOLCHAIN_BIN) {
        $candidates += Join-Path $env:RETRO68_TOOLCHAIN_BIN $executableName
    }
    if ($env:RETRO68_BUILD_DIR) {
        $candidates += Join-Path $env:RETRO68_BUILD_DIR "toolchain/bin/$executableName"
    }
    $candidates += Join-Path $HOME "Retro68-build/toolchain/bin/$executableName"
    $candidates += Join-Path $HOME "Documents/Projects/Retro68-build/toolchain/bin/$executableName"

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $executableName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "Retro68 tool not found: $Name"
}

if (-not $env:MAME_HDA -or -not (Test-Path -LiteralPath $env:MAME_HDA)) {
    throw "MAME_HDA must point to the boot hard disk template"
}

$cpu = if ($env:LOKA_MAME_CPU) { $env:LOKA_MAME_CPU } else { "68k" }
switch ($cpu) {
    "68k" { $suffix = "68K" }
    "ppc" { $suffix = "PPC" }
    default { throw "Invalid LOKA_MAME_CPU: expected 68k or ppc" }
}
$binaryPaths = @($MacBinaryPath)
# Keep the batch selection aligned with mame-dev-disk.sh.
if ($MacBinaryPath -in @("--all-loops", "--all-flows")) {
    if ($PlainDataPaths.Count) { throw "$MacBinaryPath does not accept additional files" }
    $family = if ($MacBinaryPath -eq "--all-loops") { "Loop" } else { "Flow" }
    $batchRoot = Join-Path $ProjectDirectory "build/retro68/${cpu}/Standalone/Release/tests/toolbox"
    $binaryPaths = @(
        foreach ($app in @("Scrapbook", "Hello", "Tutorial", "Mine", "Floppy")) {
            Join-Path $batchRoot "Loka${app}Standalone${family}${suffix}.bin"
        }
    )
    $PlainDataPaths = @((Join-Path $batchRoot "ASSETS.LRP"))
}
if ($MacBinaryPath -eq "--all") {
    if ($PlainDataPaths.Count) { throw "--all does not accept additional files" }
    $binaryPaths = @(
        foreach ($app in @("HelloWorld/LokaHello", "MineSweeper/LokaMine", "SimpleViewer/LokaSimpleViewer", "SimpleText/LokaSimpleText", "FloppyBird/LokaFloppyBird", "SmirkBench/LokaSmirkBench", "LazyList/LokaLazyList", "Tutorial/LokaTutorial", "ScrapbookUI/ScrapbookUI", "SmirkyCard/LokaSmirkyCard")) {
            Join-Path $ProjectDirectory "build/retro68/${cpu}/Release/example/${app}${suffix}.bin"
        }
    )
    $PlainDataPaths = @(
        Join-Path $ProjectDirectory "build/retro68/${cpu}/Release/example/ScrapbookUI/ASSETS.LRP"
        # Manifest readers in mame-*-disk* and SmirkyCard/CMakeLists.txt stay parallel.
        foreach ($line in Get-Content -LiteralPath (Join-Path $ProjectDirectory "example/SmirkyCard/disk-scripts.txt")) {
            $script = $line.Trim()
            if (!$script -or $script.StartsWith("#")) { continue }
            Join-Path $ProjectDirectory "example/SmirkyCard/$script"
        }
        # Same grammar as the adjacent script manifest; pictures are data forks.
        foreach ($line in Get-Content -LiteralPath (Join-Path $ProjectDirectory "example/SmirkyCard/disk-pictures.txt")) {
            $script = $line.Trim()
            if (!$script -or $script.StartsWith("#")) { continue }
            Join-Path $ProjectDirectory "example/SmirkyCard/assets/$script.PICT"
        }
    )
}
$resolvedMacBinaries = @(
    foreach ($path in $binaryPaths) { (Resolve-Path -LiteralPath $path).Path }
)
$resolvedPlainData = @(
    foreach ($path in $PlainDataPaths) {
        (Resolve-Path -LiteralPath $path).Path
    }
)
$mameHome = if ($env:MAME_HOMEPATH) { $env:MAME_HOMEPATH } else {
    Join-Path $HOME ".mame"
}
$controlDirectory = if ($env:MAME_CONTROL_DIR) { $env:MAME_CONTROL_DIR } else {
    Join-Path $mameHome "loka"
}
$developmentDisk = if ($env:MAME_DEV_HDA) { $env:MAME_DEV_HDA } else {
    Join-Path $ProjectDirectory "build/mame-dev/LokaDev.hd"
}
$temporaryDisk = "$developmentDisk.$PID"
$hfsHome = Join-Path $controlDirectory "hfsutils"
$hformat = Find-Retro68Tool "hformat"
$hcopy = Find-Retro68Tool "hcopy"
$humount = Find-Retro68Tool "humount"

New-Item -ItemType Directory -Path (Split-Path -Parent $developmentDisk) -Force | Out-Null
New-Item -ItemType Directory -Path $hfsHome -Force | Out-Null
Copy-Item -LiteralPath $env:MAME_HDA -Destination $temporaryDisk -Force
# hformat opens the image read-write; Copy-Item reproduced the template's
# attributes and the template is deliberately read-only.
Set-ItemProperty -LiteralPath $temporaryDisk -Name IsReadOnly -Value $false

$originalHome = $env:HOME
try {
    $env:HOME = $hfsHome
    & $hformat -l LokaDev $temporaryDisk 1
    if ($LASTEXITCODE) { throw "hformat failed with exit code $LASTEXITCODE" }
    foreach ($path in $resolvedMacBinaries) {
        & $hcopy -m $path ":"
        if ($LASTEXITCODE) { throw "hcopy failed with exit code $LASTEXITCODE" }
    }
    foreach ($path in $resolvedPlainData) {
        & $hcopy -r $path ":"
        if ($LASTEXITCODE) { throw "hcopy failed with exit code $LASTEXITCODE" }
    }
    & $humount
    if ($LASTEXITCODE) { throw "humount failed with exit code $LASTEXITCODE" }
    Move-Item -LiteralPath $temporaryDisk -Destination $developmentDisk -Force
} finally {
    $env:HOME = $originalHome
    Remove-Item -LiteralPath $temporaryDisk -Force -ErrorAction SilentlyContinue
}

Write-Output "Prepared SCSI development disk: $developmentDisk"

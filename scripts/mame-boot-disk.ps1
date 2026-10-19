param(
    [switch]$All,

    [Parameter(Position = 0)]
    [string]$MacBinaryPath,

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$PlainDataPaths = @()
)

$ErrorActionPreference = "Stop"

$ScriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDirectory = Split-Path -Parent $ScriptDirectory
. (Join-Path $ScriptDirectory "mame-boot-copy.ps1")
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

$machine = if ($env:MAME_MACHINE) { $env:MAME_MACHINE } else { "macplus" }
$mameHome = if ($env:MAME_HOMEPATH) { $env:MAME_HOMEPATH } else {
    Join-Path $HOME ".mame"
}
$controlDirectory = if ($env:MAME_CONTROL_DIR) { $env:MAME_CONTROL_DIR } else {
    Join-Path $mameHome "loka"
}
$bootDisk = if ($env:MAME_BOOT_HDA) { $env:MAME_BOOT_HDA } else {
    Join-Path $ProjectDirectory "build/mame-run/$machine/Boot.hd"
}
$bootDisk = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($bootDisk)

$macBinaries = @()
$plainData = @()

if ($All) {
    $releaseBuildRoot = Join-Path $ProjectDirectory "build/retro68/68k/Release/example"
    $allApps = @(
        "HelloWorld/LokaHello68K.bin",
        "MineSweeper/LokaMine68K.bin",
        "SimpleViewer/LokaSimpleViewer68K.bin",
        "SimpleText/LokaSimpleText68K.bin",
        "FloppyBird/LokaFloppyBird68K.bin",
        "SmirkBench/LokaSmirkBench68K.bin",
        "LazyList/LokaLazyList68K.bin",
        "Tutorial/LokaTutorial68K.bin",
        "ScrapbookUI/ScrapbookUI68K.bin",
        "SmirkyCard/LokaSmirkyCard68K.bin"
    )
    foreach ($rel in $allApps) {
        $p = Join-Path $releaseBuildRoot $rel
        if (-not (Test-Path -LiteralPath $p)) {
            throw "Application binary not found: $p (build Retro68 68K first)"
        }
        $macBinaries += (Resolve-Path -LiteralPath $p).Path
    }
    $assets = Join-Path $ProjectDirectory "example/ScrapbookUI/ASSETS.LRP"
    if (Test-Path -LiteralPath $assets) {
        $plainData += (Resolve-Path -LiteralPath $assets).Path
    }
    # Manifest readers in mame-*-disk* and SmirkyCard/CMakeLists.txt stay parallel.
    foreach ($line in Get-Content -LiteralPath (Join-Path $ProjectDirectory "example/SmirkyCard/disk-scripts.txt")) {
        $script = $line.Trim()
        if (!$script -or $script.StartsWith("#")) { continue }
        $smirkyCardScript = Join-Path $ProjectDirectory "example/SmirkyCard/$script"
        if (Test-Path -LiteralPath $smirkyCardScript) {
            $plainData += (Resolve-Path -LiteralPath $smirkyCardScript).Path
        }
    }
    # Same grammar as the adjacent script manifest; pictures are data forks.
    foreach ($line in Get-Content -LiteralPath (Join-Path $ProjectDirectory "example/SmirkyCard/disk-pictures.txt")) {
        $script = $line.Trim()
        if (!$script -or $script.StartsWith("#")) { continue }
        $smirkyCardScript = Join-Path $ProjectDirectory "example/SmirkyCard/assets/$script.PICT"
        if (Test-Path -LiteralPath $smirkyCardScript) {
            $plainData += (Resolve-Path -LiteralPath $smirkyCardScript).Path
        }
    }
} elseif ($MacBinaryPath) {
    $macBinaries += (Resolve-Path -LiteralPath $MacBinaryPath).Path
    foreach ($p in $PlainDataPaths) {
        $plainData += (Resolve-Path -LiteralPath $p).Path
    }
} else {
    throw "Usage: mame-boot-disk.ps1 -All | <MacBinaryPath> [PlainDataPaths ...]"
}

Prepare-LokaBootCopy $env:MAME_HDA $bootDisk

$hfsHome = Join-Path $controlDirectory "hfsutils"
$hmount = Find-Retro68Tool "hmount"
$hcopy = Find-Retro68Tool "hcopy"
$hls = Find-Retro68Tool "hls"
$hmkdir = Find-Retro68Tool "hmkdir"
$humount = Find-Retro68Tool "humount"

New-Item -ItemType Directory -Path $hfsHome -Force | Out-Null

$staging = "$bootDisk.staging"
if (Test-Path -LiteralPath $staging) {
    throw "staging: failed (destination needs attention)"
}
$mounted = $false
$published = $false
$originalHome = $env:HOME
try {
    Copy-Item -LiteralPath $bootDisk -Destination $staging -Force
    Set-ItemProperty -LiteralPath $staging -Name IsReadOnly -Value $false
    $env:HOME = $hfsHome
    $mounted = $true
    & $hmount $staging
    if ($LASTEXITCODE) { throw "hmount failed with exit code $LASTEXITCODE" }

    $destDir = ":Loka:"
    & $hls -d ":Loka" 2>&1 | Out-Null
    if ($LASTEXITCODE) {
        & $hmkdir ":Loka"
    }

    foreach ($bin in $macBinaries) {
        & $hcopy -m $bin $destDir
        if ($LASTEXITCODE) { throw "hcopy failed for $bin with exit code $LASTEXITCODE" }
    }
    # Parallel with hfs_copy_data in hfs-disk.sh; other plain data stay flat.
    $picturesDirectory = $null
    foreach ($data in $plainData) {
        $dataDestination = $destDir
        if ($data.Replace('\', '/') -match '(^|/)example/SmirkyCard/assets/[^/]+\.PICT$') {
            if (-not $picturesDirectory) {
                $picturesDirectory = $destDir + "Pictures:"
                # A boot disk is repopulated in place, so the folder may already exist.
                # hls exits 0 for a missing name, so list the parent instead.
                $pictureLs = Find-Retro68Tool "hls"
                $parentListing = @(& $pictureLs -1a $picturesDirectory.Substring(0, $picturesDirectory.Length - "Pictures:".Length) 2>$null)
                if ($parentListing -notcontains "Pictures") {
                    $pictureMkdir = Find-Retro68Tool "hmkdir"
                    & $pictureMkdir $picturesDirectory.TrimEnd(':')
                    if ($LASTEXITCODE) { throw "hmkdir failed with exit code $LASTEXITCODE" }
                }
            }
            $dataDestination = $picturesDirectory
        }
        & $hcopy -r $data $dataDestination
        if ($LASTEXITCODE) { throw "hcopy failed for $data with exit code $LASTEXITCODE" }
    }
    & $humount
    if ($LASTEXITCODE) { throw "humount failed with exit code $LASTEXITCODE" }
    $mounted = $false
    Move-Item -LiteralPath $staging -Destination $bootDisk -Force
    $published = $true
} finally {
    if ($mounted) { & $humount 2>$null | Out-Null }
    $env:HOME = $originalHome
    if (-not $published) {
        Remove-Item -LiteralPath $staging -Force -ErrorAction SilentlyContinue
        [Console]::Error.WriteLine("staging: failed (previous disk kept)")
    }
}

if ($All) {
    Write-Output "Staged all 68K applications to boot disk ($destDir): $bootDisk"
} else {
    Write-Output "Staged to boot disk ($destDir): $bootDisk"
}

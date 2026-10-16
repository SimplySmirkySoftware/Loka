param(
    [ValidateSet("Build", "Stage", "Verify", "Release")]
    [string]$Action = "Verify",

    [ValidateSet("x86", "x64", "arm64")]
    [string]$Architecture,

    [string]$BuildDirectory,

    [string]$StageDirectory,

    [int]$TimeoutSeconds = 120,

    [ValidateSet("modern", "xp")]
    [string]$TargetProfile = "modern"
)

$ErrorActionPreference = "Stop"
# Windows PowerShell 4.0 (the Windows 8.1 XP build host) ends a -File script
# that dies on an unhandled terminating error with exit code 0; 5.0 and later
# return 1. Report the failure explicitly so every caller sees it.
trap {
    [Console]::Error.WriteLine($_)
    exit 1
}

$ScriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDirectory = Split-Path -Parent $ScriptDirectory
$CatalogName = "standalone-flow-catalog.tsv"
$PackagedCatalog = Join-Path $ScriptDirectory $CatalogName
$IsPackagedVerifier = Test-Path -LiteralPath $PackagedCatalog
$IsReleasePackage = $Action -eq "Release"

function Resolve-ProjectPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return Join-Path $ProjectDirectory $Path
}

# The catalog key is the application directory in build output and packages.
function Get-ApplicationExecutable([string]$Root, [string]$Key, [string]$Target) {
    return Join-Path (Join-Path $Root $Key) ($Target + ".exe")
}

function Get-PeArchitecture([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) {
            throw "Not a PE executable: $Path"
        }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) {
            throw "Invalid PE signature: $Path"
        }
        $machine = $reader.ReadUInt16()
        switch ($machine) {
            0x014C { return "x86" }
            0x8664 { return "x64" }
            0xAA64 { return "arm64" }
            default { throw ("Unsupported PE machine type 0x{0:X4}: {1}" -f $machine, $Path) }
        }
    } finally {
        $stream.Dispose()
    }
}

function Assert-XpSubsystemVersion([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.BinaryReader($stream)
        # The architecture check has already validated the DOS and PE signatures.
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        # The optional header follows the signature and 20-byte COFF header.
        $optionalHeader = $peOffset + 24
        $stream.Position = $optionalHeader
        $magic = $reader.ReadUInt16()
        if ($magic -ne 0x10B -and $magic -ne 0x20B) {
            throw "Invalid PE optional header: $Path"
        }
        # These offsets are shared by PE32 and PE32+.
        $stream.Position = $optionalHeader + 48
        $major = $reader.ReadUInt16()
        $minor = $reader.ReadUInt16()
        if ($major -ne 5 -or $minor -ne 1) {
            throw ("Expected PE subsystem version 5.01 for profile xp, but {0} contains {1}.{2:D2}." -f
                $Path, $major, $minor)
        }
    } finally {
        $stream.Dispose()
    }
}

function Assert-ExecutableArchitecture([string]$Path, [string]$Expected) {
    $actual = Get-PeArchitecture $Path
    if ($actual -ne $Expected) {
        throw ("Expected a {0} PE executable, but {1} contains {2}." -f $Expected, $Path, $actual)
    }
    if ($TargetProfile -eq "xp") {
        if ($actual -ne "x86") {
            throw "Profile xp requires architecture x86."
        }
        Assert-XpSubsystemVersion $Path
    }
}

function Assert-FileCopy([string]$Source, [string]$Destination) {
    $sourceFile = Get-Item -LiteralPath $Source
    $destinationFile = Get-Item -LiteralPath $Destination
    if ($sourceFile.Length -ne $destinationFile.Length `
        -or (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash `
            -ne (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash) {
        throw "Staged file does not match its source: $Destination"
    }
}

function Get-CompilerEnvironmentArchitecture {
    $compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $compiler) {
        throw "cl.exe is not available. Run this script from Visual Studio Command Line Tools."
    }
    if (-not $env:VSCMD_ARG_TGT_ARCH) {
        throw "The Visual Studio target architecture is unavailable."
    }

    $target = $env:VSCMD_ARG_TGT_ARCH.ToLowerInvariant()
    if ($target -eq "i386") {
        $target = "x86"
    }
    if ($target -notin @("arm64", "x64", "x86")) {
        throw "Unsupported Visual Studio target architecture: $target"
    }
    return $target
}

function Assert-CompilerEnvironment([string]$Expected) {
    if ($TargetProfile -eq "xp" -and $env:VCToolsVersion -notlike "14.1*") {
        throw 'Profile xp requires the v141 toolset: Visual Studio 2017 Build Tools 15.9 with "Windows XP support for C++" installed.'
    }
    $target = Get-CompilerEnvironmentArchitecture
    if ($target -ne $Expected) {
        throw ("The requested architecture is {0}, but this Visual Studio environment targets {1}." -f
            $Expected, $target)
    }
    if ($env:VCToolsInstallDir -and $env:VSCMD_ARG_HOST_ARCH) {
        $hostTarget = $env:VSCMD_ARG_HOST_ARCH.ToLowerInvariant()
        if ($hostTarget -eq "amd64") {
            $hostTarget = "x64"
        }
        $expectedCompiler = Join-Path $env:VCToolsInstallDir `
            ("bin\Host" + $hostTarget + "\" + $Expected + "\cl.exe")
        if (-not (Test-Path -LiteralPath $expectedCompiler -PathType Leaf)) {
            throw ("The Visual Studio {0} C++ compiler is not installed: {1}" -f
                $Expected, $expectedCompiler)
        }
    }
}

function Read-SharedText([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    } finally {
        $stream.Dispose()
    }
}

function Read-SharedBytes([string]$Path) {
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    try {
        $bytes = New-Object byte[] $stream.Length
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) {
                throw "Unexpected end of audit while reading: $Path"
            }
            $offset += $read
        }
        return ,$bytes
    } finally {
        $stream.Dispose()
    }
}

function Assert-SuccessAudit([string]$Path, [string]$ExpectedPath) {
    $actual = Read-SharedBytes $Path
    $expected = [System.IO.File]::ReadAllBytes($ExpectedPath)
    if ($actual.Length -ne $expected.Length) {
        throw "Standalone audit does not match the tracked expected audit: $ExpectedPath"
    }
    for ($index = 0; $index -lt $expected.Length; ++$index) {
        if ($actual[$index] -ne $expected[$index]) {
            throw "Standalone audit does not match the tracked expected audit: $ExpectedPath"
        }
    }
}

function Get-StandaloneCatalog([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Standalone Flow catalog not found: $Path"
    }

    $entries = @()
    $keys = New-Object 'System.Collections.Generic.HashSet[string]'
    $targets = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        if (-not $line -or $line.StartsWith("#")) {
            continue
        }
        $fields = $line.Split("`t")
        if ($fields.Length -ne 3) {
            throw "Invalid Standalone Flow catalog line: $line"
        }
        $key = $fields[0]
        $target = $fields[1]
        $expectedAudit = $fields[2].Replace("/", [System.IO.Path]::DirectorySeparatorChar)
        if ($key -notmatch '^[a-z0-9_-]+$') {
            throw "Invalid Standalone Flow catalog key: $key"
        }
        if ($target -notmatch '^[A-Za-z0-9_]+$') {
            throw "Invalid Standalone Flow catalog target for '$key': $target"
        }
        if ($fields[2] -notmatch '^tests/scenarios/expected/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+\.audit$' `
            -or $fields[2].Contains("..")) {
            throw "Invalid Standalone Flow expected audit for '$key': $($fields[2])"
        }
        if (-not $keys.Add($key) -or -not $targets.Add($target)) {
            throw "Duplicate Standalone Flow catalog key or target: $line"
        }
        $entries += [PSCustomObject]@{
            Key = $key
            Target = $target
            ExpectedAudit = $expectedAudit
        }
    }
    if ($entries.Count -ne 5) {
        throw "Standalone Flow catalog must contain five runnable applications; found $($entries.Count)."
    }
    return ,$entries
}

# SmirkyCard's card scripts, in copy order. This reader stays parallel with
# example/SmirkyCard/CMakeLists.txt (which places them beside the Win32 exe),
# scripts/mame-*-disk*, scripts/release/classic-disks.py and
# scripts/macos/standalone-release-stage.sh: surrounding whitespace is
# trimmed, blank lines and full-line # comments are skipped.
function Get-SmirkyCardFiles {
    param([string]$ManifestName, [string]$Suffix = "")
    $manifest = Join-Path $ProjectDirectory "example/SmirkyCard/$ManifestName"
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
        throw "SmirkyCard script manifest not found: $manifest"
    }
    $scripts = @()
    $names = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($line in [System.IO.File]::ReadAllLines($manifest)) {
        $name = $line.Trim()
        if (-not $name -or $name.StartsWith("#")) {
            continue
        }
        if ($name -eq "." -or $name -eq ".." -or $name.IndexOfAny([char[]]"/\:") -ge 0) {
            throw "Invalid SmirkyCard script name: $name"
        }
        if (-not $names.Add($name)) {
            throw "Duplicate SmirkyCard script: $name"
        }
        $scripts += ($name + $Suffix)
    }
    if ($scripts.Count -eq 0) {
        throw "SmirkyCard script manifest lists no scripts: $manifest"
    }
    return ,$scripts
}

if ($IsPackagedVerifier -and $Action -ne "Verify") {
    throw "The staged verifier supports only the Verify action."
}

# Resolve once before deriving profile-specific preset and directory names.
if (-not $Architecture -and -not $IsPackagedVerifier) {
    $Architecture = Get-CompilerEnvironmentArchitecture
}
if ($TargetProfile -eq "xp" -and $Architecture -and $Architecture -ne "x86") {
    throw "Profile xp requires architecture x86."
}
$profileArchitecture = if ($TargetProfile -eq "xp") { "$Architecture-xp" } else { $Architecture }
$configurePreset = if ($Architecture) { "win32-standalone-$profileArchitecture-release" } else { $null }
$buildPreset = if ($Architecture) {
    if ($IsReleasePackage) {
        "win32-standalone-loop-$profileArchitecture-release"
    } else {
        "win32-standalone-flow-$profileArchitecture-release"
    }
} else { $null }
if (-not $BuildDirectory -and $Architecture) {
    $BuildDirectory = "build/win32/standalone/presentation/$profileArchitecture/Release"
}
if (-not $StageDirectory -and $Architecture) {
    $StageDirectory = if ($IsReleasePackage) {
        "build/release/win32-$profileArchitecture"
    } else {
        "build/presentation/win32-$profileArchitecture-release"
    }
}

$buildRoot = if ($BuildDirectory) { Resolve-ProjectPath $BuildDirectory } else { $null }
$stageRoot = if ($IsPackagedVerifier) { $ScriptDirectory } else { Resolve-ProjectPath $StageDirectory }
$buildCatalog = if ($buildRoot) { Join-Path $buildRoot $CatalogName } else { $null }
$catalogPath = if ($IsPackagedVerifier) { $PackagedCatalog } else { $buildCatalog }

if (-not $IsPackagedVerifier) {
    Assert-CompilerEnvironment $Architecture
    Push-Location $ProjectDirectory
    try {
        & cmake --preset $configurePreset
        if ($LASTEXITCODE) {
            throw "CMake configure failed with exit code $LASTEXITCODE."
        }
        & cmake --build --preset $buildPreset
        if ($LASTEXITCODE) {
            throw "CMake build failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}

$catalog = Get-StandaloneCatalog $catalogPath
if (-not $Architecture) {
    $firstExecutable = Get-ApplicationExecutable $stageRoot $catalog[0].Key $catalog[0].Target
    $Architecture = Get-PeArchitecture $firstExecutable
}

if ($IsReleasePackage) {
    $loopExecutableRoot = Join-Path $buildRoot "standalone-loop"
    $loopAssets = Join-Path $loopExecutableRoot "scrapbook/ASSETS.LRP"
    $simpleViewer = Join-Path $buildRoot "example/SimpleViewer/LokaSimpleViewerWin32.exe"
    $simpleText = Join-Path $buildRoot "example/SimpleText/LokaSimpleTextWin32.exe"
    $smirkyCardRoot = Join-Path $buildRoot "example/SmirkyCard"
    $smirkyCard = Join-Path $smirkyCardRoot "LokaSmirkyCardWin32.exe"
    $smirkyCardScripts = (Get-SmirkyCardFiles "disk-scripts.txt") + (Get-SmirkyCardFiles "disk-pictures.txt" ".PNG")
    foreach ($entry in $catalog) {
        $loopTarget = $entry.Target.Replace("StandaloneFlow", "StandaloneLoop")
        $loopExecutable = Get-ApplicationExecutable $loopExecutableRoot $entry.Key $loopTarget
        if (-not (Test-Path -LiteralPath $loopExecutable -PathType Leaf)) {
            throw "Autonomous loop executable not found: $loopExecutable"
        }
        Assert-ExecutableArchitecture $loopExecutable $Architecture
    }
    if (-not (Test-Path -LiteralPath $simpleViewer -PathType Leaf)) {
        throw "SimpleViewer Release executable not found: $simpleViewer"
    }
    Assert-ExecutableArchitecture $simpleViewer $Architecture
    if (-not (Test-Path -LiteralPath $simpleText -PathType Leaf)) {
        throw "SimpleText Release executable not found: $simpleText"
    }
    Assert-ExecutableArchitecture $simpleText $Architecture
    if (-not (Test-Path -LiteralPath $smirkyCard -PathType Leaf)) {
        throw "SmirkyCard Release executable not found: $smirkyCard"
    }
    Assert-ExecutableArchitecture $smirkyCard $Architecture
    foreach ($name in $smirkyCardScripts) {
        $smirkyCardScript = Join-Path $smirkyCardRoot $name
        if (-not (Test-Path -LiteralPath $smirkyCardScript -PathType Leaf)) {
            throw "SmirkyCard card script not found: $smirkyCardScript"
        }
    }
    if (-not (Test-Path -LiteralPath $loopAssets -PathType Leaf)) {
        throw "Autonomous loop assets not found: $loopAssets"
    }

    . (Join-Path $ScriptDirectory "presentation-stage.ps1")
    $populateRelease = {
        param([string]$Destination)

        foreach ($entry in $catalog) {
            $loopTarget = $entry.Target.Replace("StandaloneFlow", "StandaloneLoop")
            $sourceExecutable = Get-ApplicationExecutable $loopExecutableRoot $entry.Key $loopTarget
            New-Item -ItemType Directory -Path (Join-Path $Destination $entry.Key) | Out-Null
            $destinationExecutable = Get-ApplicationExecutable $Destination $entry.Key $loopTarget
            Copy-Item -LiteralPath $sourceExecutable -Destination $destinationExecutable
            Assert-ExecutableArchitecture $destinationExecutable $Architecture
            Assert-FileCopy $sourceExecutable $destinationExecutable
        }
        New-Item -ItemType Directory -Path (Join-Path $Destination "simpleviewer") | Out-Null
        Copy-Item -LiteralPath $simpleViewer `
            -Destination (Join-Path $Destination "simpleviewer/LokaSimpleViewerWin32.exe")
        New-Item -ItemType Directory -Path (Join-Path $Destination "simpletext") | Out-Null
        Copy-Item -LiteralPath $simpleText `
            -Destination (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe")
        Assert-ExecutableArchitecture `
            (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe") $Architecture
        Assert-FileCopy $simpleText `
            (Join-Path $Destination "simpletext/LokaSimpleTextWin32.exe")
        # SmirkyCard reads its card scripts from beside the executable, as the
        # Win32 build places them.
        New-Item -ItemType Directory -Path (Join-Path $Destination "smirkycard") | Out-Null
        Copy-Item -LiteralPath $smirkyCard `
            -Destination (Join-Path $Destination "smirkycard/LokaSmirkyCardWin32.exe")
        Assert-ExecutableArchitecture `
            (Join-Path $Destination "smirkycard/LokaSmirkyCardWin32.exe") $Architecture
        Assert-FileCopy $smirkyCard `
            (Join-Path $Destination "smirkycard/LokaSmirkyCardWin32.exe")
        foreach ($name in $smirkyCardScripts) {
            Copy-Item -LiteralPath (Join-Path $smirkyCardRoot $name) `
                -Destination (Join-Path $Destination ("smirkycard/" + $name))
            Assert-FileCopy (Join-Path $smirkyCardRoot $name) `
                (Join-Path $Destination ("smirkycard/" + $name))
        }
        Copy-Item -LiteralPath $loopAssets `
            -Destination (Join-Path $Destination "scrapbook/ASSETS.LRP")
        Assert-ExecutableArchitecture `
            (Join-Path $Destination "simpleviewer/LokaSimpleViewerWin32.exe") $Architecture
        Assert-FileCopy $simpleViewer `
            (Join-Path $Destination "simpleviewer/LokaSimpleViewerWin32.exe")
        Assert-FileCopy $loopAssets (Join-Path $Destination "scrapbook/ASSETS.LRP")
        $sourceVersionMatch = Select-String -LiteralPath (Join-Path $ProjectDirectory "CMakeLists.txt") `
            -Pattern '^project\(Loka VERSION ([0-9.]+) LANGUAGES CXX\)$'
        if (-not $sourceVersionMatch) {
            throw "Could not read the Loka source version from CMakeLists.txt"
        }
        $sourceVersion = $sourceVersionMatch.Matches[0].Groups[1].Value
        [System.IO.File]::WriteAllText(
            (Join-Path $Destination "README.txt"),
            "Loka $sourceVersion Release applications`r`n`r`n" +
            "Profile: $TargetProfile`r`n`r`n" +
            "The five StandaloneLoop applications run their UI tour repeatedly.`r`n" +
            "Open an application's folder and launch its executable.`r`n" +
            "Keep each executable with its folder: its LOG.TXT and resources belong there.`r`n" +
            "Close a loop application's window to stop it. SimpleViewer and SimpleText remain interactive.`r`n" +
            "SmirkyCard (experimental) loads MAIN.JS beside it; its buttons open MINES.JS and VIEWER.JS.`r`n")
    }.GetNewClosure()
    Install-LokaPresentationStageDirectory `
        -StageRoot $stageRoot -Populate $populateRelease
    Write-Output "Staged five autonomous loops plus SimpleViewer, SimpleText and SmirkyCard ($Architecture): $stageRoot"
    exit 0
}

$builtExecutableRoot = if ($buildRoot) { Join-Path $buildRoot "standalone-flow" } else { $null }
$builtAssets = if ($builtExecutableRoot) { Join-Path $builtExecutableRoot "scrapbook/ASSETS.LRP" } else { $null }

if (-not $IsPackagedVerifier) {
    foreach ($entry in $catalog) {
        $builtExecutable = Get-ApplicationExecutable $builtExecutableRoot $entry.Key $entry.Target
        if (-not (Test-Path -LiteralPath $builtExecutable -PathType Leaf)) {
            throw "Standalone Flow executable not found: $builtExecutable"
        }
        Assert-ExecutableArchitecture $builtExecutable $Architecture
    }
    if (-not (Test-Path -LiteralPath $builtAssets -PathType Leaf)) {
        throw "Standalone Flow assets not found: $builtAssets"
    }

    if ($Action -eq "Build") {
        Write-Output "Built five $Architecture Standalone Flow Release executables: $builtExecutableRoot"
        exit 0
    }

    . (Join-Path $ScriptDirectory "presentation-stage.ps1")
    $sourceVerifier = $MyInvocation.MyCommand.Path
    $populateStage = {
        param([string]$Destination)

        $expectedRoot = Join-Path $Destination "expected"
        New-Item -ItemType Directory -Path $expectedRoot | Out-Null
        Copy-Item -LiteralPath $buildCatalog -Destination (Join-Path $Destination $CatalogName)
        Copy-Item -LiteralPath $sourceVerifier -Destination (Join-Path $Destination "Verify-StandaloneFlow.ps1")
        foreach ($entry in $catalog) {
            $sourceExecutable = Get-ApplicationExecutable $builtExecutableRoot $entry.Key $entry.Target
            New-Item -ItemType Directory -Path (Join-Path $Destination $entry.Key) | Out-Null
            $destinationExecutable = Get-ApplicationExecutable $Destination $entry.Key $entry.Target
            $sourceExpected = Join-Path $ProjectDirectory $entry.ExpectedAudit
            $destinationExpected = Join-Path $expectedRoot ($entry.Key + ".audit")
            Copy-Item -LiteralPath $sourceExecutable -Destination $destinationExecutable
            Copy-Item -LiteralPath $sourceExpected -Destination $destinationExpected
            Assert-ExecutableArchitecture $destinationExecutable $Architecture
            Assert-FileCopy $sourceExecutable $destinationExecutable
            Assert-FileCopy $sourceExpected $destinationExpected
        }
        Copy-Item -LiteralPath $builtAssets -Destination (Join-Path $Destination "scrapbook/ASSETS.LRP")
        Assert-FileCopy $buildCatalog (Join-Path $Destination $CatalogName)
        Assert-FileCopy $builtAssets (Join-Path $Destination "scrapbook/ASSETS.LRP")
        Assert-FileCopy $sourceVerifier (Join-Path $Destination "Verify-StandaloneFlow.ps1")
        [System.IO.File]::WriteAllText(
            (Join-Path $Destination "README.txt"),
            "Loka Standalone Flow verification`r`nProfile: $TargetProfile`r`n")
    }.GetNewClosure()
    Install-LokaPresentationStageDirectory -StageRoot $stageRoot -Populate $populateStage

    if ($Action -eq "Stage") {
        Write-Output "Staged five portable $Architecture Standalone Flow Release executables: $stageRoot"
        exit 0
    }
}

$stagedAssets = Join-Path $stageRoot "scrapbook/ASSETS.LRP"
if (-not (Test-Path -LiteralPath $stagedAssets -PathType Leaf)) {
    throw "Staged Standalone Flow assets not found: $stagedAssets"
}
foreach ($entry in $catalog) {
    $stagedExecutable = Get-ApplicationExecutable $stageRoot $entry.Key $entry.Target
    $expectedAuditPath = Join-Path (Join-Path $stageRoot "expected") `
        ($entry.Key + ".audit")
    if (-not (Test-Path -LiteralPath $stagedExecutable -PathType Leaf)) {
        throw "Staged executable not found: $stagedExecutable"
    }
    if (-not (Test-Path -LiteralPath $expectedAuditPath -PathType Leaf)) {
        throw "Staged expected audit not found: $expectedAuditPath"
    }
    Assert-ExecutableArchitecture $stagedExecutable $Architecture
}

$actualRoot = Join-Path $stageRoot "actual"
New-Item -ItemType Directory -Path $actualRoot -Force | Out-Null
foreach ($entry in $catalog) {
    Remove-Item -LiteralPath (Join-Path $actualRoot ($entry.Key + ".audit")) `
        -Force -ErrorAction SilentlyContinue
}

foreach ($entry in $catalog) {
    $applicationRoot = Join-Path $stageRoot $entry.Key
    $auditPath = Join-Path $applicationRoot "LOG.TXT"
    $stagedExecutable = Get-ApplicationExecutable $stageRoot $entry.Key $entry.Target
    $expectedAuditPath = Join-Path (Join-Path $stageRoot "expected") ($entry.Key + ".audit")
    $actualAuditPath = Join-Path $actualRoot ($entry.Key + ".audit")
    Remove-Item -LiteralPath $auditPath -Force -ErrorAction SilentlyContinue

    $process = $null
    $previousAuditFixture = $env:LOKA_STANDALONE_AUDIT_FIXTURE
    try {
        if ($env:LOKA_STANDALONE_AUDIT_FIXTURE_DIR) {
            $env:LOKA_STANDALONE_AUDIT_FIXTURE = Join-Path `
                $env:LOKA_STANDALONE_AUDIT_FIXTURE_DIR ($entry.Key + ".audit")
        }
        $process = Start-Process -FilePath $stagedExecutable -WorkingDirectory $applicationRoot -PassThru
        $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
        while ([DateTime]::UtcNow -lt $deadline) {
            if (Test-Path -LiteralPath $auditPath) {
                try {
                    $content = Read-SharedText $auditPath
                } catch [System.IO.IOException] {
                    # fopen creates the file before the app closes its
                    # exclusive write handle. Treat that interval as pending.
                    Start-Sleep -Milliseconds 50
                    continue
                }
                if ($content -match "(?m)^terminal status=(failed|canceled)\r?$") {
                    Copy-Item -LiteralPath $auditPath -Destination $actualAuditPath
                    throw "$($entry.Key) Standalone Flow reported terminal status $($Matches[1])."
                }
                if ($content -match "(?m)^terminal status=succeeded\r?$") {
                    Copy-Item -LiteralPath $auditPath -Destination $actualAuditPath
                    Assert-SuccessAudit $actualAuditPath $expectedAuditPath
                    Write-Output "Runtime-verified Win32 Standalone Flow: $($entry.Key) ($Architecture)"
                    break
                }
            }
            if ($process.HasExited) {
                throw ("{0} Standalone Flow exited before publishing a success audit (exit code {1})." -f
                    $entry.Key, $process.ExitCode)
            }
            Start-Sleep -Milliseconds 200
        }
        if (-not (Test-Path -LiteralPath $actualAuditPath)) {
            throw "Timed out after $TimeoutSeconds seconds waiting for $($entry.Key) Standalone Flow success."
        }
    } finally {
        if ($process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            $process.WaitForExit()
        }
        $env:LOKA_STANDALONE_AUDIT_FIXTURE = $previousAuditFixture
    }
    Remove-Item -LiteralPath $auditPath -Force -ErrorAction SilentlyContinue
}

Write-Output "Runtime-verified all five $Architecture Win32 Standalone Flow Release executables: $actualRoot"

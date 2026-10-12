param(
    [ValidateSet('Prepare', 'Unpack', 'Stage', 'Identity', 'List', 'Cleanup')]
    [string]$Mode,
    [string]$WorkDirectory,
    [ValidateSet('x64', 'x86')][string]$Architecture,
    [ValidateSet('modern', 'xp')][string]$TargetProfile,
    [string]$EnvironmentCommand
)
$ErrorActionPreference = 'Stop'
# Windows PowerShell 4.0 otherwise reports some terminating -File errors as 0.
trap { [Console]::Error.WriteLine($_); exit 1 }

if ($TargetProfile -eq 'xp' -and $Architecture -ne 'x86') { throw 'xp requires x86' }
# Prepare/Cleanup remove recursively: only the one derived work directory is legal.
if ($WorkDirectory -notmatch '^C:[\\/]loka-release[\\/][0-9a-f]{12}-(x64|x86)-(modern|xp)$') {
    throw 'Invalid remote work directory'
}
$WorkDirectory = $WorkDirectory.Replace('/', [IO.Path]::DirectorySeparatorChar)
$source = Join-Path $WorkDirectory 'source'
$name = "win32-$Architecture"
if ($TargetProfile -eq 'xp') { $name += '-xp' }
$stage = Join-Path $source "build/release/$name"
$identity = Join-Path $WorkDirectory 'toolchain.txt'
$helper = Join-Path $WorkDirectory 'remote.ps1'
$utf8 = New-Object System.Text.UTF8Encoding($false)

function Get-FileRecord([string]$Path, [string]$Relative) {
    $file = Get-Item -LiteralPath $Path -Force
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $Path" }
    return @{ path = $Relative; size = $file.Length; sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
}

switch ($Mode) {
    'Prepare' {
        if (Test-Path -LiteralPath $WorkDirectory) { Remove-Item -LiteralPath $WorkDirectory -Recurse -Force }
        New-Item -ItemType Directory -Path $WorkDirectory -Force | Out-Null
    }
    'Unpack' {
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $WorkDirectory 'source.zip'), $source)
    }
    'Stage' {
        if ($EnvironmentCommand -notmatch '^[a-zA-Z]:\\[a-zA-Z0-9 ._\\-]+\.cmd$') { throw 'Invalid environment command path' }
        if (-not (Test-Path -LiteralPath $EnvironmentCommand -PathType Leaf)) { throw 'Environment command missing' }
        $runner = Join-Path $WorkDirectory 'stage.cmd'
        # CALL preserves the rig environment in this cmd process. Each external
        # command has its own status check; no pipeline masks native failures.
        $lines = @(
            '@echo off',
            ('call "{0}"' -f $EnvironmentCommand),
            'if not "%errorlevel%"=="0" exit /b %errorlevel%',
            ('cd /d "{0}"' -f $source),
            'if not "%errorlevel%"=="0" exit /b %errorlevel%',
            ('powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -Mode Identity -WorkDirectory "{1}" -Architecture {2} -TargetProfile {3}' -f $helper, $WorkDirectory, $Architecture, $TargetProfile),
            'if not "%errorlevel%"=="0" exit /b %errorlevel%',
            ('powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts\win32-standalone-flow.ps1 -Action Release -Architecture {0} -TargetProfile {1}' -f $Architecture, $TargetProfile),
            'exit /b %errorlevel%'
        )
        [IO.File]::WriteAllText($runner, (($lines -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
        & cmd /d /c "`"$runner`""
        if ($LASTEXITCODE -ne 0) { throw "Remote stage failed: $LASTEXITCODE" }
    }
    'Identity' {
        $version = @(& cmake --version)
        if ($LASTEXITCODE -ne 0 -or $version.Count -eq 0) { throw 'cmake --version failed' }
        $line = 'VCToolsVersion={0}; WindowsSDKVersion={1}; cmake={2}; OS={3}; PowerShell={4}' -f $env:VCToolsVersion, $env:WindowsSDKVersion, $version[0], [Environment]::OSVersion.VersionString, $PSVersionTable.PSVersion
        [IO.File]::WriteAllText($identity, ($line + "`r`n"), $utf8)
        Write-Output $line
    }
    'List' {
        $stageRoot = Get-Item -LiteralPath $stage -Force
        if ($stageRoot.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Stage is a reparse point' }
        $files = @(Get-ChildItem -LiteralPath $stage -Recurse -Force | ForEach-Object {
            if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $($_.FullName)" }
            if (-not $_.PSIsContainer) {
                Get-FileRecord $_.FullName ($_.FullName.Substring($stageRoot.FullName.Length + 1).Replace('\', '/'))
            }
        })
        if ($files.Count -eq 0) { throw 'Empty release stage' }
        $inventory = @{ files = $files; toolchain = (Get-FileRecord $identity 'toolchain.txt') }
        [IO.File]::WriteAllText((Join-Path $WorkDirectory 'inventory.json'), (ConvertTo-Json -InputObject $inventory -Depth 5), $utf8)
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::CreateFromDirectory($stage, (Join-Path $WorkDirectory 'stage.zip'))
    }
    'Cleanup' { Remove-Item -LiteralPath $WorkDirectory -Recurse -Force }
}
exit 0

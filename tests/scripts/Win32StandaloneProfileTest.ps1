param(
    [switch]$Child,
    [string]$Root,
    [string]$Action = "Stage",
    [string]$Architecture = "x86",
    [string]$TargetProfile = "xp",
    [string]$Toolset = "14.16.27023",
    [switch]$InferArchitecture,
    [switch]$DefaultProfile
)

$ErrorActionPreference = "Stop"
$ProjectDirectory = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Subject = Join-Path $ProjectDirectory "scripts/win32-standalone-flow.ps1"

if ($Child) {
    # Functions stand in for native tools on both Windows PowerShell and pwsh.
    function cl.exe { }
    function cmake {
        $expectedArchitecture = if ($TargetProfile -eq "xp") { "x86-xp" } else { $Architecture }
        $expectedPreset = if ($args[0] -eq "--build") {
            $vehicle = if ($Action -eq "Release") { "loop" } else { "flow" }
            "win32-standalone-$vehicle-$expectedArchitecture-release"
        } else {
            "win32-standalone-$expectedArchitecture-release"
        }
        if ($args[-1] -ne $expectedPreset) {
            throw "Wrong preset: $($args[-1]); expected $expectedPreset"
        }
        $global:LASTEXITCODE = 0
    }
    $env:VSCMD_ARG_TGT_ARCH = $Architecture
    $env:VCToolsVersion = $Toolset
    $env:VCToolsInstallDir = ""
    $parameters = @{
        Action = $Action
        BuildDirectory = (Join-Path $Root "build")
        StageDirectory = (Join-Path $Root "stage")
    }
    if (-not $InferArchitecture) { $parameters.Architecture = $Architecture }
    if (-not $DefaultProfile) { $parameters.TargetProfile = $TargetProfile }
    # Dot-source so the subject runs at this -File script's top level: its
    # GetNewClosure() populate blocks resolve script functions only there, and
    # the mocks above stay visible. The subject's exit leaves $LASTEXITCODE.
    . $Subject @parameters
    exit $LASTEXITCODE
}

$Shell = (Get-Process -Id $PID).Path
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("loka-xp-profile-" + [Guid]::NewGuid())
$Catalog = @(
    @("scrapbook", "Scrapbook", "standalone-tour"),
    @("helloworld", "HelloWorld", "toggle-action-probe"),
    @("tutorial", "Tutorial", "increment-summary-toggle"),
    @("minesweeper", "MineSweeper", "new-game-twice"),
    @("floppybird", "FloppyBird", "fixed-step-flaps")
)

function Write-TestPe([string]$Path, [int]$Major = 5, [int]$Minor = 1,
                      [uint16]$Magic = 0x10B) {
    $bytes = New-Object byte[] 512
    [BitConverter]::GetBytes([uint16]0x5A4D).CopyTo($bytes, 0)
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3C)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($bytes, 0x80)
    [BitConverter]::GetBytes([uint16]0x14C).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes($Magic).CopyTo($bytes, 0x98)
    [BitConverter]::GetBytes([uint16]$Major).CopyTo($bytes, 0x98 + 48)
    [BitConverter]::GetBytes([uint16]$Minor).CopyTo($bytes, 0x98 + 50)
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function Invoke-Profile([string[]]$Arguments, [string]$ExpectedError = "") {
    $previous = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & $Shell -NoProfile -File $PSCommandPath -Child -Root $TestRoot @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($ExpectedError) {
        # One line per record: Windows PowerShell 5.1's Out-String wraps error
        # records at the console width, splitting the expected phrase.
        $text = (@($output) | ForEach-Object { "$_" }) -join " "
        if ($code -eq 0 -or $text -notmatch $ExpectedError) {
            throw "Expected '$ExpectedError', exit=$code output=$output"
        }
    } elseif ($code -ne 0) {
        throw "Profile failed: $output"
    }
}

function Assert-Readme([string]$Expected) {
    $readme = [System.IO.File]::ReadAllText((Join-Path $TestRoot "stage/README.txt"))
    if ($readme -notmatch "(?m)^Profile: $Expected\r?$") {
        throw "Missing README profile $Expected"
    }
}

try {
    New-Item -ItemType Directory -Path (Join-Path $TestRoot "build") | Out-Null
    $lines = @()
    foreach ($entry in $Catalog) {
        $target = "Loka" + $entry[1] + "StandaloneFlowWin32"
        $lines += ($entry[0] + "`t" + $target + "`ttests/scenarios/expected/" +
            $entry[0] + "/" + $entry[2] + ".audit")
        foreach ($vehicle in @("flow", "loop")) {
            $directory = Join-Path $TestRoot ("build/standalone-" + $vehicle + "/" + $entry[0])
            New-Item -ItemType Directory -Path $directory -Force | Out-Null
            $name = if ($vehicle -eq "loop") { $target.Replace("Flow", "Loop") } else { $target }
            Write-TestPe (Join-Path $directory ($name + ".exe"))
        }
    }
    [System.IO.File]::WriteAllLines((Join-Path $TestRoot "build/standalone-flow-catalog.tsv"), $lines)
    foreach ($vehicle in @("flow", "loop")) {
        [System.IO.File]::WriteAllText((Join-Path $TestRoot ("build/standalone-" + $vehicle +
            "/scrapbook/ASSETS.LRP")), "synthetic assets")
    }
    $viewer = Join-Path $TestRoot "build/example/SimpleViewer/LokaSimpleViewerWin32.exe"
    New-Item -ItemType Directory -Path (Split-Path -Parent $viewer) -Force | Out-Null
    Write-TestPe $viewer
    $text = Join-Path $TestRoot "build/example/SimpleText/LokaSimpleTextWin32.exe"
    New-Item -ItemType Directory -Path (Split-Path -Parent $text) -Force | Out-Null
    Write-TestPe $text
    # SmirkyCard's build places each manifest script beside its executable.
    $smirkyRoot = Join-Path $TestRoot "build/example/SmirkyCard"
    New-Item -ItemType Directory -Path $smirkyRoot -Force | Out-Null
    Write-TestPe (Join-Path $smirkyRoot "LokaSmirkyCardWin32.exe")
    $smirkyScripts = @([System.IO.File]::ReadAllLines(
            (Join-Path $ProjectDirectory "example/SmirkyCard/disk-scripts.txt")) |
        ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith("#") })
    $smirkyScripts += @([System.IO.File]::ReadAllLines(
            (Join-Path $ProjectDirectory "example/SmirkyCard/disk-pictures.txt")) |
        ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith("#") } |
        ForEach-Object { $_ + ".PNG" })
    foreach ($name in $smirkyScripts) {
        [System.IO.File]::WriteAllText((Join-Path $smirkyRoot $name), "// $name")
    }

    Invoke-Profile @("-Architecture", "x64") "Profile xp requires architecture x86"
    Invoke-Profile @("-Architecture", "arm64") "Profile xp requires architecture x86"
    Invoke-Profile @("-InferArchitecture", "-Architecture", "x64") "Profile xp requires architecture x86"
    foreach ($toolset in @("14.29.30133", "14.0", "unknown")) {
        Invoke-Profile @("-Toolset", $toolset) "Visual Studio 2017 Build Tools.*Windows XP support for C\+\+"
    }
    Invoke-Profile @()
    Assert-Readme "xp"
    Invoke-Profile @("-InferArchitecture")
    Assert-Readme "xp"
    Invoke-Profile @("-Action", "Release")
    Assert-Readme "xp"
    foreach ($name in @("LokaSmirkyCardWin32.exe") + $smirkyScripts) {
        if (-not (Test-Path -LiteralPath (Join-Path $TestRoot ("stage/smirkycard/" + $name)) -PathType Leaf)) {
            throw "Release did not stage smirkycard/$name"
        }
    }
    # Release refuses a SmirkyCard whose manifest script the build did not place.
    $parked = Join-Path $smirkyRoot ($smirkyScripts[-1] + ".parked")
    Move-Item -LiteralPath (Join-Path $smirkyRoot $smirkyScripts[-1]) -Destination $parked
    Invoke-Profile @("-Action", "Release") "SmirkyCard card script not found"
    Move-Item -LiteralPath $parked -Destination (Join-Path $smirkyRoot $smirkyScripts[-1])
    $smirkyExe = Join-Path $smirkyRoot "LokaSmirkyCardWin32.exe"
    Move-Item -LiteralPath $smirkyExe -Destination ($smirkyExe + ".parked")
    Invoke-Profile @("-Action", "Release") "SmirkyCard Release executable not found"
    Move-Item -LiteralPath ($smirkyExe + ".parked") -Destination $smirkyExe

    # Each source in both vehicles, including SimpleViewer, SimpleText and SmirkyCard, must be refused.
    $executables = @(Get-ChildItem -LiteralPath (Join-Path $TestRoot "build") -Recurse -Filter *.exe)
    foreach ($exe in $executables) {
        Write-TestPe $exe.FullName 6 0
        $action = if ($exe.FullName -match "standalone-flow") { "Stage" } else { "Release" }
        Invoke-Profile @("-Action", $action) "Expected PE subsystem version 5.01"
        Assert-Readme "xp"
        Write-TestPe $exe.FullName
    }
    # Default modern retains acceptance of subsystem 6.00 and newer toolsets.
    foreach ($exe in $executables) { Write-TestPe $exe.FullName 6 0 }
    Invoke-Profile @("-TargetProfile", "modern", "-DefaultProfile", "-Toolset", "14.29.30133")
    Assert-Readme "modern"
    Invoke-Profile @("-TargetProfile", "modern", "-Action", "Release", "-Toolset", "14.29.30133")
    Assert-Readme "modern"

    # Read the production function through the AST; no test-only production door.
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($Subject, [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) { throw "Script parse failed: $errors" }
    # Evaluate the real naming statements without running CMake or writing to
    # the default directories. Integration cases above exercise explicit paths.
    $source = [System.IO.File]::ReadAllText($Subject)
    $start = $source.IndexOf('$profileArchitecture =')
    $end = $source.IndexOf('$buildRoot =', $start)
    $naming = [scriptblock]::Create($source.Substring($start, $end - $start))
    foreach ($TargetProfile in @("modern", "xp")) {
        foreach ($IsReleasePackage in @($false, $true)) {
            $Architecture = "x86"
            $BuildDirectory = $null
            $StageDirectory = $null
            . $naming
            $suffix = if ($TargetProfile -eq "xp") { "x86-xp" } else { "x86" }
            $expectedStage = if ($IsReleasePackage) { "build/release/win32-$suffix" } else {
                "build/presentation/win32-$suffix-release"
            }
            if ($BuildDirectory -ne "build/win32/standalone/presentation/$suffix/Release" -or
                $StageDirectory -ne $expectedStage) {
                throw "Wrong default directories for $TargetProfile / release=$IsReleasePackage"
            }
        }
    }
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq "Assert-XpSubsystemVersion"
    }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
    foreach ($magic in @(0x10B, 0x20B)) {
        Write-TestPe $viewer 5 1 $magic
        Assert-XpSubsystemVersion $viewer
        foreach ($version in @(@(6, 0), @(5, 0), @(5, 2))) {
            Write-TestPe $viewer $version[0] $version[1] $magic
            $refused = $false
            try { Assert-XpSubsystemVersion $viewer } catch {
                if ($_ -notmatch "Expected PE subsystem version 5.01") { throw }
                $refused = $true
            }
            if (-not $refused) { throw "Subsystem check accepted $version" }
        }
    }
    Write-Output "Win32 profiles select presets, guard toolsets/architecture, validate PE subsystem, and label Stage/Release."
} finally {
    Remove-Item -LiteralPath $TestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2

$ProjectDirectory = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Support = Join-Path $ProjectDirectory "tests/win32/SourceTreeStamp.ps1"
. $Support
$TestRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("loka-source-stamp-test-" + [Guid]::NewGuid())
$Repo = Join-Path $TestRoot "repo"
$SavedTemp = $env:TEMP
$SavedTmp = $env:TMP
$SavedIndex = $env:GIT_INDEX_FILE

function Git-Test {
    $output = & git -C $Repo @args 2>&1
    if ($LASTEXITCODE -ne 0) { throw "git failed: $output" }
    return $output
}
function Assert-Equal($Actual, $Expected, [string]$Case) {
    if ($Actual -cne $Expected) { throw "$Case`: '$Actual' != '$Expected'" }
}
function Expect-Failure([scriptblock]$Action, [string]$Fragment) {
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notlike "*$Fragment*") { throw }
        return
    }
    throw "Expected refusal containing '$Fragment'"
}

try {
    New-Item -ItemType Directory -Path $Repo | Out-Null
    # Put every throwaway index in an inspectable directory, including failure paths.
    $env:TEMP = $TestRoot
    $env:TMP = $TestRoot
    $env:GIT_INDEX_FILE = $null
    Git-Test init --quiet | Out-Null
    Git-Test config user.name 'Source stamp test' | Out-Null
    Git-Test config user.email 'source-stamp@example.invalid' | Out-Null
    Git-Test config core.autocrlf false | Out-Null
    $Tracked = Join-Path $Repo "tracked.txt"
    [IO.File]::WriteAllText($Tracked, "initial`n")
    Git-Test add tracked.txt | Out-Null
    Git-Test -c commit.gpgsign=false commit --quiet -m initial | Out-Null
    $Clean = Get-SourceTreeId $Repo
    Assert-Equal $Clean (Git-Test rev-parse 'HEAD^{tree}') 'clean tree'
    [IO.File]::WriteAllText($Tracked, "edited`n")
    $Dirty = Get-SourceTreeId $Repo
    if ($Dirty -ceq $Clean) { throw "Tracked edit was invisible" }
    Assert-Equal (Git-Test diff --cached) $null 'unstaged real index unchanged'
    Git-Test add tracked.txt | Out-Null
    $StagedBefore = (Git-Test diff --cached) -join "`n"
    Assert-Equal (Get-SourceTreeId $Repo) $Dirty 'staged and unstaged tree agree'
    Assert-Equal ((Git-Test diff --cached) -join "`n") $StagedBefore 'staged real index unchanged'
    [IO.File]::WriteAllText($Tracked, "initial`n")
    Assert-Equal (Get-SourceTreeId $Repo) $Clean 'reverted working file ignores staged edit'
    Assert-Equal ((Git-Test diff --cached) -join "`n") $StagedBefore 'staged edit preserved'
    [IO.File]::WriteAllText((Join-Path $Repo 'untracked.txt'), "ignored`n")
    Assert-Equal (Get-SourceTreeId $Repo) $Clean 'untracked file ignored'
    # A file added but not committed is tracked: editing it must move the id.
    $Added = Join-Path $Repo 'added.txt'
    [IO.File]::WriteAllText($Added, "first`n")
    Git-Test add added.txt | Out-Null
    $AddedFirst = Get-SourceTreeId $Repo
    if ($AddedFirst -ceq $Clean) { throw "Newly added file was invisible" }
    [IO.File]::WriteAllText($Added, "second`n")
    $AddedSecond = Get-SourceTreeId $Repo
    if ($AddedSecond -ceq $AddedFirst) { throw "Edit to a newly added file was invisible" }
    Git-Test rm --cached --quiet added.txt | Out-Null
    Remove-Item -LiteralPath $Added
    Assert-Equal (Get-SourceTreeId $Repo) $Clean 'removing the added file restores the id'
    Expect-Failure { Get-SourceTreeId $TestRoot } 'git rev-parse --absolute-git-dir failed'

    $env:GIT_INDEX_FILE = Join-Path $TestRoot 'caller-index'
    try {
        Assert-Equal (Get-SourceTreeId $Repo) $Clean 'caller index override ignored'
        Expect-Failure { Get-SourceTreeId $TestRoot } 'git rev-parse --absolute-git-dir failed'
        Assert-Equal $env:GIT_INDEX_FILE (Join-Path $TestRoot 'caller-index') 'caller index environment restored'
    } finally {
        $env:GIT_INDEX_FILE = $null
    }
    $SavedPath = $env:PATH
    try {
        $env:PATH = ''
        Expect-Failure { Get-SourceTreeId $Repo } 'git'
    } finally {
        $env:PATH = $SavedPath
    }

    $Stamp = Join-Path $TestRoot 'stamp.txt'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Support -Write $Stamp -SourceDirectory $Repo
    Assert-Equal $LASTEXITCODE 0 'writer succeeds'
    Assert-Equal ([IO.File]::ReadAllText($Stamp).Trim()) $Clean 'stamp round-trip'
    [IO.File]::WriteAllText($Tracked, "edited`n")
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Support -Write $Stamp -SourceDirectory $Repo
    Assert-Equal $LASTEXITCODE 0 'dirty writer succeeds'
    Assert-Equal ([IO.File]::ReadAllText($Stamp).Trim()) $Dirty 'dirty stamp round-trip'
    [IO.File]::WriteAllText($Tracked, "initial`n")
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Support -Write $Stamp -SourceDirectory $TestRoot
    Assert-Equal $LASTEXITCODE 0 'archive build succeeds'
    Assert-Equal (Test-Path -LiteralPath $Stamp) $false 'failed write removes stale stamp'
    $PowerShell = (Get-Command powershell.exe -CommandType Application).Source
    $SavedPath = $env:PATH
    try {
        [IO.File]::WriteAllText($Stamp, $Clean)
        $env:PATH = ''
        & $PowerShell -NoProfile -ExecutionPolicy Bypass -File $Support -Write $Stamp -SourceDirectory $Repo
        Assert-Equal $LASTEXITCODE 0 'build without git succeeds'
        Assert-Equal (Test-Path -LiteralPath $Stamp) $false 'no git removes stale stamp'
    } finally {
        $env:PATH = $SavedPath
    }

    # Exercise the actual runner build statements, stopping before any platform work.
    # Like Win32StandaloneProfileTest, parse the production script rather than add a test door.
    $Tokens = $null
    $ParseErrors = $null
    $Runner = Join-Path $ProjectDirectory 'tests/win32/run-scenario.ps1'
    $Ast = [System.Management.Automation.Language.Parser]::ParseFile($Runner, [ref]$Tokens, [ref]$ParseErrors)
    if ($ParseErrors.Count) { throw "Runner parse failed: $ParseErrors" }
    $Try = $Ast.EndBlock.Statements | Where-Object { $_ -is [System.Management.Automation.Language.TryStatementAst] } | Select-Object -Last 1
    $Statements = @($Try.Body.Statements)
    $BuildText = @()
    $InBuild = $false
    foreach ($Statement in $Statements) {
        $Text = $Statement.Extent.Text
        if ($Text.StartsWith('$Vehicle =')) { $InBuild = $true }
        if ($Text.StartsWith('if ($Example -eq "scrapbook")')) { break }
        if ($InBuild) { $BuildText += $Text }
    }
    if ($BuildText.Count -lt 5) { throw 'Runner build stage extraction failed' }
    function Fail-Stage([string]$StageName, [string]$Message) { throw "$StageName stage failed: $Message" }
    $ProjectDirectory = $Repo
    # The dot-source remains pointed at the production support script.
    $SourceSupportDirectory = Split-Path -Parent $Support
    $Build = [scriptblock]::Create(($BuildText -join "`n").Replace('$PSScriptRoot', '$SourceSupportDirectory'))
    $Example = 'helloworld'
    $Vehicles = @{ helloworld = @{ Executable = 'vehicle.exe'; OutputDirectory = 'HelloWorld' } }
    $BuildDirectory = Join-Path $Repo 'build/win32/Debug'
    $VehicleDirectory = Join-Path $BuildDirectory 'example/HelloWorld'
    New-Item -ItemType Directory -Path $VehicleDirectory -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $VehicleDirectory 'vehicle.exe'), '')
    $RunnerStamp = Join-Path $BuildDirectory 'scenario-source-stamp.txt'
    Expect-Failure { & $Build } 'build stage failed: no source stamp; run cmake --build --preset win32-tests'
    [IO.File]::WriteAllText($RunnerStamp, $Dirty)
    Expect-Failure { & $Build } 'build stage failed: vehicles were built from source tree'
    [IO.File]::WriteAllText($RunnerStamp, $Clean)
    & $Build
    $GitDirectory = Join-Path $Repo '.git'
    $HiddenGit = Join-Path $TestRoot 'hidden-git'
    Move-Item -LiteralPath $GitDirectory -Destination $HiddenGit
    try {
        Expect-Failure { & $Build } 'build stage failed: cannot read source identity:'
    } finally {
        Move-Item -LiteralPath $HiddenGit -Destination $GitDirectory
    }
    Assert-Equal (@(Get-ChildItem -LiteralPath $TestRoot -Filter 'loka-source-index-*').Count) 0 'temporary indexes and locks removed'
    Assert-Equal (@(Get-ChildItem -LiteralPath $TestRoot -Filter '*.tmp').Count) 0 'temporary stamps removed'
    Assert-Equal $env:GIT_INDEX_FILE $null 'index environment restored'
    Write-Output 'Win32 source stamps identify tracked working files and refuse stale vehicles.'
} finally {
    $env:TEMP = $SavedTemp
    $env:TMP = $SavedTmp
    $env:GIT_INDEX_FILE = $SavedIndex
    Remove-Item -LiteralPath $TestRoot -Recurse -Force -ErrorAction SilentlyContinue
}

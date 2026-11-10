param(
    [string]$Withdraw,
    [string]$Write,
    [string]$SourceDirectory = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

# Returns the tracked working tree ID, or throws with the reason it is absent.
# Tracked means "in the checkout's own index", so a file added but not yet
# committed counts; staged content never does, because `add -u` replaces
# every entry with its working content. No real index is modified.
function Get-SourceTreeId([string]$Directory) {
    # Native stderr is diagnostic text; Git's exit code decides success (PS 5.1).
    $ErrorActionPreference = "Continue"
    # PATH can hold several git.exe (hosted runners do); use the one a shell would run.
    $git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $index = Join-Path ([System.IO.Path]::GetTempPath()) ("loka-source-index-" + [Guid]::NewGuid())
    $savedIndex = $env:GIT_INDEX_FILE
    try {
        # Start from a copy of the checkout's own index (never the caller's
        # GIT_INDEX_FILE); an unborn index starts from HEAD instead.
        $gitDirectory = & $git -C $Directory rev-parse --absolute-git-dir 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "git rev-parse --absolute-git-dir failed: $($gitDirectory -join ' ')"
        }
        $ownIndex = Join-Path ([string]$gitDirectory).Trim() 'index'
        $steps = @(@('add', '-u'), @('write-tree'))
        if (Test-Path -LiteralPath $ownIndex -PathType Leaf) {
            Copy-Item -LiteralPath $ownIndex -Destination $index -ErrorAction Stop
        } else {
            $steps = @(, @('read-tree', 'HEAD')) + $steps
        }
        $env:GIT_INDEX_FILE = $index
        foreach ($arguments in $steps) {
            $result = & $git -C $Directory @arguments 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "git $($arguments -join ' ') failed: $($result -join ' ')"
            }
        }
        return ([string]$result).Trim()
    } finally {
        $env:GIT_INDEX_FILE = $savedIndex
        Remove-Item -LiteralPath $index, "$index.lock" -Force -ErrorAction SilentlyContinue
    }
}

# The build publishes a stamp in two steps around the vehicles (#1224):
# -Withdraw <stamp> runs before any vehicle compiles; it removes the stamp and
# records the tree it starts from in <stamp>.before. -Write <stamp> runs after
# every vehicle has linked and publishes the stamp only when the tree is still
# that one, so an edit made during the build leaves no stamp at all. Neither
# step fails the build: a tree Git cannot describe simply gets no stamp.
function Write-StampFile([string]$Path, [string]$Id) {
    $temporary = "$Path.$([Guid]::NewGuid()).tmp"
    try {
        [System.IO.File]::WriteAllText($temporary, "$Id`n", (New-Object System.Text.UTF8Encoding($false)))
        Move-Item -LiteralPath $temporary -Destination $Path -Force -ErrorAction Stop
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

if ($Withdraw -or $Write) {
    $Stamp = if ($Withdraw) { $Withdraw } else { $Write }
    $Before = "$Stamp.before"
    try {
        # Invalidate first: a failed capture must never preserve an old baseline.
        Remove-Item -LiteralPath $Stamp -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $Stamp) { throw "cannot remove $Stamp" }
        if ($Withdraw) {
            Remove-Item -LiteralPath $Before -Force -ErrorAction SilentlyContinue
            Write-StampFile $Before (Get-SourceTreeId $SourceDirectory)
        } else {
            if (-not (Test-Path -LiteralPath $Before -PathType Leaf)) {
                throw "the build did not record the tree it started from"
            }
            $startId = ([System.IO.File]::ReadAllText($Before)).Trim()
            $id = Get-SourceTreeId $SourceDirectory
            if ($id -cne $startId) {
                throw "tracked sources changed during the build ($startId -> $id); build again"
            }
            Write-StampFile $Stamp $id
            Write-Output "Scenario source tree $id"
        }
    } catch {
        Write-Warning "No source stamp: $($_.Exception.Message)"
    } finally {
        if ($Write) { Remove-Item -LiteralPath $Before -Force -ErrorAction SilentlyContinue }
    }
    exit 0
}

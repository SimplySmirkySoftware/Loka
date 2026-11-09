param(
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
    $git = (Get-Command git -CommandType Application -ErrorAction Stop).Source
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

if ($Write) {
    $temporary = "$Write.$([Guid]::NewGuid()).tmp"
    try {
        # Invalidate first: a failed capture must never preserve an old baseline.
        Remove-Item -LiteralPath $Write -Force -ErrorAction Stop
    } catch [System.Management.Automation.ItemNotFoundException] {
        # First build has no previous stamp.
    } catch {
        Write-Warning "No source stamp: $($_.Exception.Message)"
        exit 0
    }
    try {
        $id = Get-SourceTreeId $SourceDirectory
        [System.IO.File]::WriteAllText($temporary, "$id`n", (New-Object System.Text.UTF8Encoding($false)))
        Move-Item -LiteralPath $temporary -Destination $Write -Force -ErrorAction Stop
        Write-Output "Scenario source tree $id"
    } catch {
        Write-Warning "No source stamp: $($_.Exception.Message)"
    } finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
    exit 0
}

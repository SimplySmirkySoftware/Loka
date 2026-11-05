# Launch-URL probe for the Win32 rail (B3 page 2b R4). Verification only.
#
# Builds tests/win32/URLReceiveProbe.cpp from a fresh build directory and
# checks, through %TEMP%\loka-url-receive-probe.log, that a URL on the command
# line reaches the receive arm exactly once.
#
#   -Mode candidate  phases 1 (direct launch), 2 (order and refusals) and
#                    3 (a temporarily registered HKCU scheme through ShellExecuteExW)
#   -Mode mutant     applies -Patch, rebuilds, and runs phase 1 only; KILLED means
#                    the probe started and wrote ready but never received the URL
#
# Run it as an interactive scheduled task in the guest's console session
# (Windows PowerShell 5.1); a window opens. Exit status 0 only when every phase
# passes (candidate) or the mutant is KILLED (mutant).
param(
  [Parameter(Mandatory = $true)][string]$Source,
  [Parameter(Mandatory = $true)][string]$Rev,
  [Parameter(Mandatory = $true)][string]$OutDir,
  [ValidateSet('candidate', 'mutant')][string]$Mode = 'candidate',
  [string]$Patch = '',
  [int]$DeadlineSeconds = 20,
  [int]$QuietSeconds = 2
)

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$manifest = Join-Path $OutDir 'manifest.txt'
Set-Content $manifest "mode=$Mode rev=$Rev started=$(Get-Date -Format o)"
$log = Join-Path $env:TEMP 'loka-url-receive-probe.log'
$keyPath = 'HKCU:\Software\Classes\lokaurlprobe'
$verdicts = New-Object System.Collections.ArrayList
$owned = New-Object System.Collections.ArrayList
$cleanupErrors = New-Object System.Collections.ArrayList
$keyCreated = $false
$patchApplied = $false
$exe = $null

# Write-Host, not the pipeline: helpers that note must still return one value.
function Note([string]$line) { Add-Content $manifest $line; Write-Host $line }
function Verdict([string]$phase, [string]$result, [string]$detail) {
  [void]$verdicts.Add("$phase $result $detail"); Note "VERDICT $phase $result $detail"
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LokaProbeShell
{
  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  struct SHELLEXECUTEINFOW
  {
    public int cbSize; public uint fMask; public IntPtr hwnd;
    [MarshalAs(UnmanagedType.LPWStr)] public string lpVerb;
    [MarshalAs(UnmanagedType.LPWStr)] public string lpFile;
    [MarshalAs(UnmanagedType.LPWStr)] public string lpParameters;
    [MarshalAs(UnmanagedType.LPWStr)] public string lpDirectory;
    public int nShow; public IntPtr hInstApp; public IntPtr lpIDList;
    [MarshalAs(UnmanagedType.LPWStr)] public string lpClass;
    public IntPtr hkeyClass; public uint dwHotKey; public IntPtr hIconOrMonitor; public IntPtr hProcess;
  }
  [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
  static extern bool ShellExecuteExW(ref SHELLEXECUTEINFOW info);
  [DllImport("shell32.dll")]
  public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
  [DllImport("kernel32.dll")]
  public static extern uint GetProcessId(IntPtr process);
  // SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI. The caller owns hProcess.
  public static bool Open(string file, out IntPtr process, out int error)
  {
    SHELLEXECUTEINFOW info = new SHELLEXECUTEINFOW();
    info.cbSize = Marshal.SizeOf(typeof(SHELLEXECUTEINFOW));
    info.fMask = 0x00000040 | 0x00000400;
    info.lpVerb = "open";
    info.lpFile = file;
    info.nShow = 1;
    error = 0;
    bool ok = ShellExecuteExW(ref info);
    if (!ok)
      error = Marshal.GetLastWin32Error();
    process = info.hProcess;
    return ok;
  }
  public static void AssociationsChanged() { SHChangeNotify(0x08000000, 0, IntPtr.Zero, IntPtr.Zero); }
}
'@

function Invoke-Build([string]$dir) {
  $bat = Join-Path $OutDir "build-$Mode.bat"
  $lines = @(
    '@echo off',
    "cd /d `"$Source`"",
    'for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSPATH=%%i',
    'if "%PROCESSOR_ARCHITECTURE%"=="ARM64" (set VCARCH=arm64) else (set VCARCH=x64)',
    'call "%VSPATH%\VC\Auxiliary\Build\vcvarsall.bat" %VCARCH% > NUL || exit /b 11',
    "cmake --preset win32-debug -B `"$dir`" || exit /b 12",
    "cmake --build `"$dir`" --target LokaURLReceiveProbeWin32 || exit /b 13",
    'cl 2>&1 | findstr /i "version"',
    'exit /b 0'
  )
  Set-Content -Encoding ASCII $bat $lines
  if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
  $out = Join-Path $OutDir "build-$Mode.log"
  cmd.exe /c "`"$bat`" > `"$out`" 2>&1"
  if ($LASTEXITCODE -ne 0) { throw "build failed with $LASTEXITCODE (see $out)" }
  $built = Join-Path $dir 'example\HelloWorld\LokaURLReceiveProbeWin32.exe'
  if (-not (Test-Path $built)) { throw "probe executable missing after build: $built" }
  return $built
}

function Read-Log { if (Test-Path $log) { return @(Get-Content $log) } else { return @() } }

function Wait-Line([scriptblock]$match, [int]$seconds) {
  $deadline = (Get-Date).AddSeconds($seconds)
  while ((Get-Date) -lt $deadline) {
    $hit = @(Read-Log | Where-Object $match)
    if ($hit.Count -gt 0) { return $hit[0] }
    Start-Sleep -Milliseconds 200
  }
  return $null
}

# Owned process record: the managed Process (disposed) and, for the shell phase,
# the SafeProcessHandle returned by ShellExecuteExW (closed exactly once).
function Add-Owned($process, $handle) {
  $record = New-Object psobject -Property @{ Process = $process; Handle = $handle; Pid = $process.Id }
  [void]$owned.Add($record)
  Note "OWNED pid=$($record.Pid) path=$($process.Path) start=$($process.StartTime.ToString('o')) session=$($process.SessionId)"
  return $record
}

function Stop-Owned($record) {
  try {
    if (-not $record.Process.HasExited) {
      $record.Process.Kill()
      if (-not $record.Process.WaitForExit(10000)) { throw "pid $($record.Pid) did not exit" }
    }
    Note "STOPPED pid=$($record.Pid) exit=$($record.Process.ExitCode)"
  } finally {
    if ($record.Handle) { $record.Handle.Dispose(); $record.Handle = $null }
    $record.Process.Dispose()
    $record.Process = $null
  }
}

# Checks one owned launch: ready with its pid and nonce, the expected received
# lines in order with clock=1, then a quiet interval with the process alive.
function Test-Launch($record, [string]$nonce, [string[]]$expected) {
  $pidText = "pid=$($record.Pid) "
  $ready = Wait-Line { $_ -like "ready $pidText*" } $DeadlineSeconds
  if (-not $ready) {
    if ($record.Process.HasExited) { return "FAIL exited before ready (exit $($record.Process.ExitCode))" }
    return "FAIL no ready within ${DeadlineSeconds}s"
  }
  if ($ready -notlike "* rev=$Rev cmd=*") { return "FAIL ready revision or format: $ready" }
  if ($ready -notlike "*$nonce*") { return "FAIL ready command line lacks the nonce: $ready" }
  $last = $expected[$expected.Count - 1]
  $got = Wait-Line { $_ -eq "received ${pidText}clock=1 $last" } $DeadlineSeconds
  if (-not $got) {
    if ($record.Process.HasExited) { return "FAIL exited before received (exit $($record.Process.ExitCode))" }
    return "NORECEIVE"
  }
  Start-Sleep -Seconds $QuietSeconds
  if ($record.Process.HasExited) { return "FAIL exited during the quiet interval" }
  $received = @(Read-Log | Where-Object { $_ -like "received $pidText*" })
  $want = @($expected | ForEach-Object { "received ${pidText}clock=1 $_" })
  if ($received.Count -ne $want.Count) { return "FAIL received $($received.Count) lines, want $($want.Count): $($received -join ' | ')" }
  for ($i = 0; $i -lt $want.Count; ++$i) {
    if ($received[$i] -ne $want[$i]) { return "FAIL line $i is '$($received[$i])', want '$($want[$i])'" }
  }
  return "PASS"
}

function Start-Direct([string]$arguments) {
  $p = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
  return Add-Owned $p $null
}

$exitCode = 1
try {
  # 0. Provenance.
  $head = (git -C $Source rev-parse HEAD).Trim()
  $tracked = @(git -C $Source status --porcelain --untracked-files=no)
  Note "source=$Source head=$head tracked-dirty=$($tracked.Count) acp=$([System.Text.Encoding]::Default.CodePage)"
  Note "os=$([Environment]::OSVersion.VersionString) arch=$env:PROCESSOR_ARCHITECTURE user=$([Environment]::UserName) sid=$([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) session=$((Get-Process -Id $PID).SessionId) host=$($PSVersionTable.PSVersion) apartment=$([Threading.Thread]::CurrentThread.GetApartmentState())"
  if ($head -ne $Rev) { throw "PROVENANCE head $head is not $Rev" }
  if ($tracked.Count -ne 0) { throw "PROVENANCE tracked files are dirty before the run" }
  if ($Mode -eq 'mutant') {
    if (-not (Test-Path $Patch)) { throw "mutant mode needs -Patch" }
    Note "patch=$Patch sha256=$((Get-FileHash -Algorithm SHA256 $Patch).Hash)"
    git -C $Source apply --check $Patch
    if ($LASTEXITCODE -ne 0) { throw "patch does not apply" }
    git -C $Source apply $Patch
    if ($LASTEXITCODE -ne 0) { throw "patch apply failed" }
    $patchApplied = $true
    $changed = @(git -C $Source diff --name-only)
    Note "patched=$($changed -join ',')"
    if ($changed.Count -ne 1 -or $changed[0] -ne 'win32/src/Win32App.cpp') { throw "patch touched unexpected paths" }
    git -C $Source apply -R --check $Patch
    if ($LASTEXITCODE -ne 0) { throw "tree does not match the patch exactly" }
  }
  $exe = Invoke-Build (Join-Path $Source "build\url-probe-$Mode")
  Note "exe=$exe sha256=$((Get-FileHash -Algorithm SHA256 $exe).Hash)"

  # 0b. Preconditions.
  $running = @(Get-Process LokaURLReceiveProbeWin32 -ErrorAction SilentlyContinue)
  if ($running.Count -gt 0) { throw "UNAVAILABLE a probe process is already running: $(($running | ForEach-Object { $_.Id }) -join ',')" }
  if (Test-Path $keyPath) { throw "UNAVAILABLE $keyPath already exists" }
  if (Test-Path $log) { Remove-Item $log }
  $seed = Get-Random

  # 1. Direct launch.
  $n1 = "direct-$seed"
  $r1 = Start-Direct "`"lokaurlprobe:$n1`""
  $v1 = Test-Launch $r1 $n1 @("lokaurlprobe:$n1")
  Stop-Owned $r1
  if ($Mode -eq 'mutant') {
    if ($v1 -eq 'NORECEIVE') { Verdict 'phase1' 'KILLED' 'ready seen, receive timed out with the probe alive' }
    else { Verdict 'phase1' 'SURVIVED' $v1 }
  } else {
    if ($v1 -eq 'NORECEIVE') { $v1 = "FAIL no received within ${DeadlineSeconds}s" }
    Verdict 'phase1' ($v1 -split ' ')[0] $v1

    # 2. Order and refusals.
    $n2 = "order-$seed"
    $r2 = Start-Direct "C:\Users\x\doc.txt -flag `"lokaurlprobe:a-$n2`" lokaurlprobe:b-$n2"
    $v2 = Test-Launch $r2 $n2 @("lokaurlprobe:a-$n2", "lokaurlprobe:b-$n2")
    Stop-Owned $r2
    if ($v2 -eq 'NORECEIVE') { $v2 = "FAIL no received within ${DeadlineSeconds}s" }
    Verdict 'phase2' ($v2 -split ' ')[0] $v2

    # 3. Registered scheme through ShellExecuteExW.
    $command = "`"$exe`" `"%1`""
    New-Item -Path $keyPath | Out-Null
    $keyCreated = $true
    Set-ItemProperty -Path $keyPath -Name '(default)' -Value 'URL:lokaurlprobe'
    New-ItemProperty -Path $keyPath -Name 'URL Protocol' -Value '' | Out-Null
    New-Item -Path "$keyPath\shell\open\command" -Force | Out-Null
    Set-ItemProperty -Path "$keyPath\shell\open\command" -Name '(default)' -Value $command
    $readBack = (Get-ItemProperty -Path "$keyPath\shell\open\command").'(default)'
    Note "registered command=$readBack"
    if ($readBack -ne $command) { throw "registered command does not read back" }
    [LokaProbeShell]::AssociationsChanged()
    $n3 = "reg-$seed"
    $handle = [IntPtr]::Zero
    $err = 0
    $ok = [LokaProbeShell]::Open("lokaurlprobe:$n3", [ref]$handle, [ref]$err)
    Note "shellexecute ok=$ok error=$err handle=$handle"
    if (-not $ok) {
      Verdict 'phase3' 'UNAVAILABLE' "ShellExecuteExW failed with error $err while the key reads back correctly"
    } elseif ($handle -eq [IntPtr]::Zero) {
      Verdict 'phase3' 'FAIL' 'unsupported instrumentation: ShellExecuteExW returned no process handle'
    } else {
      $safe = New-Object Microsoft.Win32.SafeHandles.SafeProcessHandle($handle, $true)
      $pid3 = [LokaProbeShell]::GetProcessId($handle)
      $p3 = Get-Process -Id $pid3
      $r3 = Add-Owned $p3 $safe
      $v3 = Test-Launch $r3 $n3 @("lokaurlprobe:$n3")
      Stop-Owned $r3
      if ($v3 -eq 'NORECEIVE') { $v3 = "FAIL no received within ${DeadlineSeconds}s" }
      Verdict 'phase3' ($v3 -split ' ')[0] $v3
    }
  }
} catch {
  $message = $_.Exception.Message
  if ($message -like 'UNAVAILABLE*') { Verdict 'setup' 'UNAVAILABLE' $message } else { Verdict 'setup' 'FAIL' $message }
} finally {
  foreach ($record in $owned) {
    try { if ($record.Process) { Stop-Owned $record } } catch { [void]$cleanupErrors.Add("process $($record.Pid): $($_.Exception.Message)") }
  }
  $owned.Clear()
  $left = @(Get-Process LokaURLReceiveProbeWin32 -ErrorAction SilentlyContinue)
  if ($left.Count -gt 0) { [void]$cleanupErrors.Add("unowned probe processes still alive (not killed): $(($left | ForEach-Object { $_.Id }) -join ',')") }
  if ($keyCreated) {
    try {
      Remove-Item -Recurse -Path $keyPath
      [LokaProbeShell]::AssociationsChanged()
      if (Test-Path $keyPath) { throw 'key still present' }
      Note 'key removed'
    } catch { [void]$cleanupErrors.Add("registry: $($_.Exception.Message)") }
  }
  if ($patchApplied) {
    try {
      git -C $Source apply -R $Patch
      if ($LASTEXITCODE -ne 0) { throw 'reverse apply failed' }
      $file = (git -C $Source hash-object win32/src/Win32App.cpp).Trim()
      $want = (git -C $Source rev-parse "HEAD:win32/src/Win32App.cpp").Trim()
      if ($file -ne $want) { throw "restored content $file is not HEAD's $want" }
      if (@(git -C $Source status --porcelain --untracked-files=no).Count -ne 0) { throw 'tracked files dirty after restore' }
      Note 'patch reversed, content verified'
    } catch { [void]$cleanupErrors.Add("patch: $($_.Exception.Message)") }
  }
  if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir 'probe.log') }
  foreach ($e in $cleanupErrors) { Verdict 'cleanup' 'FAIL' $e }
  $results = @($verdicts | ForEach-Object { ($_ -split ' ')[1] })
  if ($Mode -eq 'mutant') { $good = $results.Count -eq 1 -and $results[0] -eq 'KILLED' }
  else { $good = $results.Count -eq 3 -and @($results | Where-Object { $_ -ne 'PASS' }).Count -eq 0 }
  if ($good) { $exitCode = 0 }
  Note "RESULT exit=$exitCode"
  Set-Content (Join-Path $OutDir 'done.txt') "exit=$exitCode"
}
exit $exitCode

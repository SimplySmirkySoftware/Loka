# Manual characterization only; deliberately not registered with ctest or CI.
# Build/stage the regular SmirkyCard app with LAPTOP.JS and Pictures alongside it.
# Example (Windows PowerShell 5.1):
# .\tests\scripts\Win32LaptopFocusCharacterize.ps1 -AppPath C:\stage\LokaSmirkyCard.exe -OutputPath C:\logs\laptop.txt
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$AppPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [int]$TimeoutSeconds = 20
)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class LaptopInput {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO {
        public uint cbSize, flags;
        public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret;
        public RECT rcCaret;
    }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort wVk, wScan;
        public uint dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION data; }
    public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hwnd, EnumProc callback, IntPtr param);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
    [DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint thread, ref GUITHREADINFO info);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint count, INPUT[] inputs, int size);
    public static string Text(IntPtr hwnd) {
        StringBuilder text = new StringBuilder(4096);
        GetWindowTextW(hwnd, text, text.Capacity);
        return text.ToString();
    }
    public static string ClassName(IntPtr hwnd) {
        StringBuilder text = new StringBuilder(256);
        GetClassNameW(hwnd, text, text.Capacity);
        return text.ToString();
    }
    public static IntPtr[] Buttons(IntPtr parent) {
        List<IntPtr> buttons = new List<IntPtr>();
        EnumChildWindows(parent, delegate(IntPtr hwnd, IntPtr unused) {
            if (IsWindowVisible(hwnd) && String.Equals(ClassName(hwnd), "Button", StringComparison.OrdinalIgnoreCase))
                buttons.Add(hwnd);
            return true;
        }, IntPtr.Zero);
        return buttons.ToArray();
    }
    public static bool Click(IntPtr hwnd) {
        RECT rect;
        if (!GetWindowRect(hwnd, out rect) || rect.right <= rect.left || rect.bottom <= rect.top) return false;
        // Absolute virtual-desktop coordinates; movement and button edges are all real input.
        int x = (rect.left + (rect.right - rect.left) / 2 - GetSystemMetrics(76)) * 65535 / Math.Max(1, GetSystemMetrics(78) - 1);
        int y = (rect.top + (rect.bottom - rect.top) / 2 - GetSystemMetrics(77)) * 65535 / Math.Max(1, GetSystemMetrics(79) - 1);
        INPUT[] inputs = new INPUT[3];
        inputs[0].data.mi.dx = x; inputs[0].data.mi.dy = y;
        inputs[0].data.mi.dwFlags = 0xC001; // MOVE | ABSOLUTE | VIRTUALDESK
        inputs[1].data.mi.dwFlags = 0x0002; // LEFTDOWN
        inputs[2].data.mi.dwFlags = 0x0004; // LEFTUP
        return SendInput(3, inputs, Marshal.SizeOf(typeof(INPUT))) == 3;
    }
    public static bool Key(ushort key) {
        INPUT[] inputs = new INPUT[2];
        inputs[0].type = inputs[1].type = 1;
        inputs[0].data.ki.wVk = inputs[1].data.ki.wVk = key;
        inputs[1].data.ki.dwFlags = 2; // KEYUP
        return SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT))) == 2;
    }
    public static bool Focus(IntPtr window, out IntPtr focus) {
        uint process;
        uint thread = GetWindowThreadProcessId(window, out process);
        GUITHREADINFO info = new GUITHREADINFO();
        info.cbSize = (uint)Marshal.SizeOf(typeof(GUITHREADINFO));
        focus = IntPtr.Zero;
        if (thread == 0 || !GetGUIThreadInfo(thread, ref info)) return false;
        focus = info.hwndFocus;
        return true;
    }
}
'@

$lines = New-Object 'System.Collections.Generic.List[string]'
function Write-Row([string]$Line) {
    $lines.Add($Line)
    Write-Output $Line
    # Persist every row, including partial/skip runs.
    $lines | Set-Content -LiteralPath $OutputPath -Encoding UTF8
}
function Activate-Window([IntPtr]$Window) {
    [void][LaptopInput]::ShowWindow($Window, 9)
    [void][LaptopInput]::SetForegroundWindow($Window)
    Start-Sleep -Milliseconds 200
    return [LaptopInput]::GetForegroundWindow() -eq $Window
}
function Find-Button([IntPtr]$Window, [string]$Text) {
    return @([LaptopInput]::Buttons($Window) | Where-Object { [LaptopInput]::Text($_) -eq $Text })
}
function Record-Step([string]$Step, [IntPtr]$Window) {
    if ([LaptopInput]::GetForegroundWindow() -ne $Window) {
        Write-Row "$Step`t[skip] window is not foreground"
        return
    }
    $focus = [IntPtr]::Zero
    if (-not [LaptopInput]::Focus($Window, [ref]$focus)) {
        Write-Row "$Step`t[skip] GetGUIThreadInfo failed"
        return
    }
    $focused = if ($focus -eq [IntPtr]::Zero) { '<none>' } else {
        '{0} "{1}"' -f [LaptopInput]::ClassName($focus), [LaptopInput]::Text($focus)
    }
    $buttons = @([LaptopInput]::Buttons($Window) | ForEach-Object { '"{0}"' -f [LaptopInput]::Text($_) }) -join ' | '
    Write-Row "$Step`t$focused`t$buttons"
}

$AppPath = (Resolve-Path -LiteralPath $AppPath).Path
$OutputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
Write-Row "step`tfocused control`tvisible button texts"
# WinMain ignores commandLine. The normal MAIN.JS button opens the sidecar LAPTOP.JS.
$process = Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path -Parent $AppPath) -PassThru
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$window = [IntPtr]::Zero
while ((Get-Date) -lt $deadline -and -not $process.HasExited) {
    $process.Refresh()
    $window = $process.MainWindowHandle
    if ($window -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 100
}
$ready = $false
$reason = 'application window did not appear'
if ($window -ne [IntPtr]::Zero) {
    if (Activate-Window $window) {
        while ((Get-Date) -lt $deadline) {
            $open = @(Find-Button $window './LAPTOP.JS')
            if ($open.Count -eq 1) { break }
            Start-Sleep -Milliseconds 100
        }
        $reason = 'MAIN.JS Laptop button unavailable or ambiguous'
        if ($open.Count -eq 1) {
            if ([LaptopInput]::GetForegroundWindow() -eq $window -and [LaptopInput]::Click($open[0])) {
                while ((Get-Date) -lt $deadline) {
                    if (@(Find-Button $window 'Wide').Count -eq 1) { $ready = $true; break }
                    Start-Sleep -Milliseconds 100
                }
                $reason = 'LAPTOP.JS Wide button did not appear; check the staged sidecars'
            } else { $reason = 'MAIN.JS click could not be sent to the foreground window' }
        }
    } else { $reason = 'window cannot be made foreground' }
}
foreach ($step in @('1 mouse Wide', '2 Tab', '3 Space', '4 Enter')) {
    if (-not $ready) { Write-Row "$step`t[skip] $reason"; continue }
    if (-not (Activate-Window $window)) {
        Write-Row "$step`t[skip] window cannot be made foreground"
        continue
    }
    $focus = [IntPtr]::Zero
    if (-not [LaptopInput]::Focus($window, [ref]$focus)) {
        Write-Row "$step`t[skip] GetGUIThreadInfo failed"
        continue
    }
    $sent = $false
    switch ($step) {
        '1 mouse Wide' {
            $wide = @(Find-Button $window 'Wide')
            if ($wide.Count -eq 1) { $sent = [LaptopInput]::Click($wide[0]) }
        }
        '2 Tab' { $sent = [LaptopInput]::Key(9) }
        '3 Space' { $sent = [LaptopInput]::Key(32) }
        '4 Enter' { $sent = [LaptopInput]::Key(13) }
    }
    if (-not $sent) { Write-Row "$step`t[skip] input unavailable or SendInput failed"; continue }
    Start-Sleep -Milliseconds 400
    Record-Step $step $window
}
# Leave the characterized application open for inspection.

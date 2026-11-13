param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][int]$Left,
    [Parameter(Mandatory)][int]$Top,
    [ValidateSet(1,5,20)][int]$Notches = 1,
    [ValidateSet(10,30,100)][int]$Interval = 30,
    [ValidateSet(-1,1)][int]$Direction = 1,
    [ValidateRange(1,200)][int]$Trials = 50,
    [ValidateRange(1,240)][int]$WheelDelta = 120,
    [int]$AnchorX = 307,
    [int]$AnchorY = 289,
    [switch]$FirstZoom
)

$ErrorActionPreference = 'Stop'
if (!('ZoomDesktopProbe' -as [type])) {
  Add-Type -AssemblyName System.Drawing.Common
  Add-Type -ReferencedAssemblies @(
    [Drawing.Bitmap].Assembly.Location,
    [Drawing.Point].Assembly.Location,
    [System.ComponentModel.Win32Exception].Assembly.Location,
    [Runtime.InteropServices.Marshal].Assembly.Location
) -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public sealed class ZoomDesktopProbe : IDisposable
{
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr process);
    [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
    [DllImport("user32.dll")] private static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] private static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] private static extern void mouse_event(uint flags, uint x, uint y, int data, UIntPtr extra);

    private readonly Bitmap bitmap = new Bitmap(416, 416, PixelFormat.Format32bppArgb);
    private readonly Graphics graphics;
    private readonly byte[] pixels = new byte[416 * 416 * 4];
    public ZoomDesktopProbe() { graphics = Graphics.FromImage(bitmap); }
    public static void Key(byte key, bool down) { keybd_event(key, 0, down ? 0u : 2u, UIntPtr.Zero); }
    public static void Wheel(int delta) { mouse_event(0x800, 0, 0, delta, UIntPtr.Zero); }
    public static bool ActivateAt(IntPtr hwnd, int x, int y)
    {
        if (GetAncestor(WindowFromPoint(new Point(x, y)), 2) != hwnd) return false;
        SetCursorPos(x, y);
        mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero);
        mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero);
        return true;
    }
    public static void Focus(IntPtr hwnd)
    {
        uint foreground = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
        uint current = GetCurrentThreadId();
        bool attached = foreground != 0 && foreground != current && AttachThreadInput(current, foreground, true);
        try
        {
            SetWindowPos(hwnd, new IntPtr(-1), 0, 0, 0, 0, 0x43);
            SetForegroundWindow(hwnd);
        }
        finally { if (attached) AttachThreadInput(current, foreground, false); }
    }
    public static void RestoreZOrder(IntPtr hwnd)
    {
        SetWindowPos(hwnd, new IntPtr(-2), 0, 0, 0, 0, 0x13);
    }
    public ulong Frame(int left, int top)
    {
        graphics.CopyFromScreen(left + 112, top + 112, 0, 0, bitmap.Size);
        var data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height),
            ImageLockMode.ReadOnly, bitmap.PixelFormat);
        try { Marshal.Copy(data.Scan0, pixels, 0, pixels.Length); }
        finally { bitmap.UnlockBits(data); }
        ulong hash = 14695981039346656037UL;
        for (int i = 0; i < pixels.Length; i += 4)
            for (int c = 0; c < 3; c++) hash = (hash ^ pixels[i + c]) * 1099511628211UL;
        return hash;
    }
    public void Dispose() { graphics.Dispose(); bitmap.Dispose(); }
}
'@
}

$process = Get-Process -Id $ProcessId
$handle = $process.MainWindowHandle
if (!$handle) { throw 'The benchmark window is unavailable.' }
[ZoomDesktopProbe]::Focus($handle)
Start-Sleep -Milliseconds 150
if ([ZoomDesktopProbe]::GetForegroundWindow() -ne $handle) {
    $null = [ZoomDesktopProbe]::ActivateAt($handle,($Left+12),($Top+12))
    Start-Sleep -Milliseconds 150
}
if ([ZoomDesktopProbe]::GetForegroundWindow() -ne $handle) {
    [ZoomDesktopProbe]::RestoreZOrder($handle)
    throw "The benchmark window must have focus (expected $handle, observed $([ZoomDesktopProbe]::GetForegroundWindow()))."
}
$null = [ZoomDesktopProbe]::SetCursorPos($Left + $AnchorX, $Top + $AnchorY)
$probe = [ZoomDesktopProbe]::new()
$delta = $WheelDelta * $Direction
$references = @{}
$first = [Collections.Generic.List[double]]::new()
$last = [Collections.Generic.List[double]]::new()
$spacing = [Collections.Generic.List[double]]::new()
$skipped = 0
function Reset-View {
    [ZoomDesktopProbe]::Key(0x77,$true)
    [ZoomDesktopProbe]::Key(0x77,$false)
    Start-Sleep -Milliseconds 100
}
function Wait-ForInitialFrame([ulong]$Expected) {
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    do {
        if ($probe.Frame($Left,$Top) -eq $Expected) { return }
        Start-Sleep -Milliseconds 3
    } while ($deadline.ElapsedMilliseconds -lt 500)
    throw 'The viewport did not return to its starting frame.'
}
function Report([Collections.Generic.List[double]]$Values) {
    if (!$Values.Count) { return $null }
    $Values.Sort()
    [ordered]@{
        samples = $Values.Count
        median_ms = $Values[[Math]::Ceiling($Values.Count*.50)-1]
        p95_ms = $Values[[Math]::Ceiling($Values.Count*.95)-1]
        max_ms = $Values[$Values.Count-1]
    }
}
try {
    if ($FirstZoom) {
        if ($Notches -ne 1 -or $Trials -ne 1) { throw 'FirstZoom requires one notch and one trial.' }
        $initial = $probe.Frame($Left,$Top)
        $captures = [Collections.Generic.Dictionary[ulong,double]]::new()
        [ZoomDesktopProbe]::Key(0x12,$true)
        $clock = [Diagnostics.Stopwatch]::StartNew()
        [ZoomDesktopProbe]::Wheel($delta)
        while ($clock.ElapsedMilliseconds -lt 600) {
            if ([ZoomDesktopProbe]::GetForegroundWindow() -ne $handle) { throw 'The benchmark window lost focus.' }
            $hash = $probe.Frame($Left,$Top)
            $null = $captures.TryAdd($hash,$clock.Elapsed.TotalMilliseconds)
        }
        [ZoomDesktopProbe]::Key(0x12,$false)
        $final = $probe.Frame($Left,$Top)
        if ($final -eq $initial -or !$captures.ContainsKey($final)) { throw 'The first zoom did not produce a stable frame.' }
        [ordered]@{process=$process.ProcessName; direction=$Direction; delta=$WheelDelta; first_zoom_ms=$captures[$final]} |
            ConvertTo-Json
        return
    }
    Reset-View
    $initial = $probe.Frame($Left,$Top)
    [ZoomDesktopProbe]::Key(0x12,$true)
    for ($i=0; $i -lt $Notches; $i++) {
        [ZoomDesktopProbe]::Wheel($delta)
        Start-Sleep -Milliseconds 120
        $hash = $probe.Frame($Left,$Top)
        if ($hash -eq $initial -or $references.ContainsKey($hash)) { throw "Reference view $i did not change." }
        $references[$hash] = $i
    }
    [ZoomDesktopProbe]::Key(0x12,$false)
    for ($trial=-2; $trial -lt $Trials; $trial++) {
        Reset-View
        Wait-ForInitialFrame $initial
        $sent = [double[]]::new($Notches)
        $observed = [bool[]]::new($Notches)
        $next = 0
        $previous = -1.0
        [ZoomDesktopProbe]::Key(0x12,$true)
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while (!$observed[$Notches-1] -and $clock.Elapsed.TotalMilliseconds -lt 4000) {
            if ([ZoomDesktopProbe]::GetForegroundWindow() -ne $handle) { throw 'The benchmark window lost focus.' }
            if ($next -lt $Notches -and $clock.Elapsed.TotalMilliseconds -ge $next*$Interval) {
                $sent[$next++] = $clock.Elapsed.TotalMilliseconds
                [ZoomDesktopProbe]::Wheel($delta)
            }
            $hash = $probe.Frame($Left,$Top)
            $now = $clock.Elapsed.TotalMilliseconds
            if (!$references.ContainsKey($hash)) { continue }
            $index = $references[$hash]
            if ($index -ge $next -or $observed[$index]) { continue }
            $observed[$index] = $true
            if ($trial -ge 0) {
                if ($index -eq 0) { $first.Add($now-$sent[$index]) }
                if ($index -eq $Notches-1) { $last.Add($now-$sent[$index]) }
                if ($previous -ge 0) { $spacing.Add($now-$previous) }
            }
            $previous = $now
        }
        [ZoomDesktopProbe]::Key(0x12,$false)
        if (!$observed[$Notches-1]) { throw "The final reference frame was not observed in trial $trial." }
        if ($trial -ge 0) { $skipped += @($observed | Where-Object { !$_ }).Count }
    }
    [ordered]@{
        process = $process.ProcessName
        notches = $Notches
        direction = $Direction
        interval_ms = $Interval
        delta = $WheelDelta
        first = (Report $first)
        last = (Report $last)
        frame_interval = (Report $spacing)
        skipped = $skipped
    } | ConvertTo-Json -Depth 3
} finally {
    [ZoomDesktopProbe]::Key(0x12,$false)
    [ZoomDesktopProbe]::RestoreZOrder($handle)
    $probe.Dispose()
}

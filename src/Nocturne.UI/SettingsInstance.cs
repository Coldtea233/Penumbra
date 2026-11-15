using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Nocturne;

// Only coordinates settings windows. Never stops or replaces the desktop backend.
internal sealed class SettingsInstance : IDisposable
{
    internal const string WindowProperty = "Penumbra.Settings";
    internal const string ProtocolProperty = "Penumbra.Settings.ReplacementProtocol";
    internal const string VersionProperty = "Penumbra.Settings.Version";
    private const string IsolatedProperty = "Penumbra.Settings.Isolated";
    private const nuint SubclassId = 0x504E4249;
    private readonly string identity;
    private readonly bool isolated;
    private readonly Version version;
    private readonly Mutex mutex;
    private readonly SubclassProc callback;
    private readonly uint replacementMessage;
    private IntPtr window;
    private Func<bool>? closeForReplacement;
    private bool owned, disposed;

    internal SettingsInstance(string identity, bool isolated, Version version)
    {
        this.identity = identity;
        this.isolated = isolated;
        this.version = version;
        mutex = new Mutex(false, @"Local\" + identity);
        callback = HandleMessage;
        replacementMessage = RegisterWindowMessage("Penumbra.Settings.CloseForReplacement.v1");
        if (replacementMessage == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    internal async Task<bool> AcquireAsync()
    {
        var deadline = Stopwatch.StartNew();
        while (deadline.Elapsed < TimeSpan.FromSeconds(8))
        {
            if (TryAcquire()) return true;
            IntPtr existing = FindExistingWindow();
            if (existing != IntPtr.Zero)
            {
                if (!CanReplace(existing)) { Activate(existing); return false; }
                // New builds close without starting a tray-only backend. Legacy
                // builds receive their normal graceful close; never terminate them.
                uint message = GetProp(existing, ProtocolProperty) != IntPtr.Zero ? replacementMessage : 0x0010;
                SendMessageTimeout(existing, message, IntPtr.Zero, IntPtr.Zero, 2, 500, out _);
            }
            // UI-thread continuation preserves named-mutex thread ownership.
            await Task.Delay(100);
        }
        IntPtr pending = FindExistingWindow();
        if (pending != IntPtr.Zero) Activate(pending);
        MessageBox(IntPtr.Zero, "旧设置窗口正在处理操作，暂时无法交接。请稍后重新打开新版。", "Penumbra", 0x40);
        return false;
    }

    private bool TryAcquire()
    {
        if (owned) return true;
        try { owned = mutex.WaitOne(0); }
        catch (AbandonedMutexException) { owned = true; }
        return owned;
    }

    private IntPtr FindExistingWindow()
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((candidate, _) =>
        {
            if (GetProp(candidate, identity) == IntPtr.Zero) return true;
            if (!isolated && GetProp(candidate, IsolatedProperty) != IntPtr.Zero) return true;
            GetWindowThreadProcessId(candidate, out uint pid);
            if (pid == Environment.ProcessId) return true;
            found = candidate;
            return false;
        }, IntPtr.Zero);
        return found;
    }

    private bool CanReplace(IntPtr existing)
    {
        try
        {
            GetWindowThreadProcessId(existing, out uint pid);
            using var process = Process.GetProcessById(checked((int)pid));
            string? existingPath = process.MainModule?.FileName;
            if (existingPath is null || Environment.ProcessPath is not string currentPath) return false;
            Version? existingVersion = UnpackVersion(GetProp(existing, VersionProperty).ToInt64());
            if (existingVersion is null)
            {
                var file = FileVersionInfo.GetVersionInfo(existingPath);
                existingVersion = new Version(file.FileMajorPart, file.FileMinorPart, file.FileBuildPart, file.FilePrivatePart);
            }
            return ShouldReplace(currentPath, version, existingPath, existingVersion);
        }
        catch (Exception e) when (e is Win32Exception or InvalidOperationException or ArgumentException or IOException or UnauthorizedAccessException)
        { return false; }
    }

    internal static bool ShouldReplace(string currentPath, Version currentVersion, string existingPath, Version existingVersion)
        => currentVersion >= existingVersion &&
           (currentVersion > existingVersion || !string.Equals(currentPath, existingPath, StringComparison.OrdinalIgnoreCase));

    internal static long PackVersion(Version value)
        => ((long)(value.Major + 1) << 48) | ((long)value.Minor << 32) |
           ((long)Math.Max(0, value.Build) << 16) | (uint)Math.Max(0, value.Revision);

    internal static Version? UnpackVersion(long value)
        => value == 0 ? null : new Version((int)((ulong)value >> 48) - 1,
            (int)((value >> 32) & 0xffff), (int)((value >> 16) & 0xffff), (int)(value & 0xffff));

    internal void Attach(IntPtr hwnd, Func<bool> requestClose)
    {
        window = hwnd;
        closeForReplacement = requestClose;
        if (!SetWindowSubclass(hwnd, callback, SubclassId, 0))
            throw new Win32Exception(Marshal.GetLastWin32Error(), "无法准备设置窗口交接。");
        SetProp(hwnd, identity, (IntPtr)1);
        if (identity != WindowProperty) SetProp(hwnd, WindowProperty, (IntPtr)1);
        if (isolated) SetProp(hwnd, IsolatedProperty, (IntPtr)1);
        SetProp(hwnd, VersionProperty, (IntPtr)PackVersion(version));
        SetProp(hwnd, ProtocolProperty, (IntPtr)1);
    }

    private IntPtr HandleMessage(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data)
    {
        if (message == replacementMessage)
            return closeForReplacement?.Invoke() == true ? (IntPtr)1 : IntPtr.Zero;
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    private static void Activate(IntPtr hwnd)
    {
        if (IsIconic(hwnd)) ShowWindow(hwnd, 9);
        SetForegroundWindow(hwnd);
    }

    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        if (window != IntPtr.Zero)
        {
            RemoveWindowSubclass(window, callback, SubclassId);
            foreach (string property in new[] { identity, WindowProperty, ProtocolProperty, VersionProperty, IsolatedProperty })
                RemoveProp(window, property);
        }
        if (owned) { mutex.ReleaseMutex(); owned = false; }
        mutex.Dispose();
    }

    private delegate bool EnumWindowCallback(IntPtr hwnd, IntPtr parameter);
    private delegate IntPtr SubclassProc(IntPtr hwnd, uint message, IntPtr wParam, IntPtr lParam, nuint id, nuint data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowCallback callback, IntPtr parameter);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr GetProp(IntPtr hwnd, string name);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool SetProp(IntPtr hwnd, string name, IntPtr value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern IntPtr RemoveProp(IntPtr hwnd, string name);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wp, IntPtr lp, uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] private static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int MessageBox(IntPtr hwnd, string text, string caption, uint type);
    [DllImport("comctl32.dll", SetLastError = true)] private static extern bool SetWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id, nuint data);
    [DllImport("comctl32.dll")] private static extern bool RemoveWindowSubclass(IntPtr hwnd, SubclassProc callback, nuint id);
    [DllImport("comctl32.dll")] private static extern IntPtr DefSubclassProc(IntPtr hwnd, uint message, IntPtr wp, IntPtr lp);
}

using System.Runtime.InteropServices;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.DirectX.Direct3D11;
using Windows.Graphics.Imaging;

namespace Nocturne;

// Temporary GPU resources exist only while obtaining a single wallpaper frame.
internal static class WindowCapture
{
    internal sealed record Frame(byte[] Pixels, int Width, int Height);

    public static async Task<Frame> CaptureAsync(IntPtr window)
    {
        if (!GraphicsCaptureSession.IsSupported())
            throw new InvalidOperationException("系统不支持后台壁纸捕获。");
        var item = CreateItem(window);
        if (item.Size.Width <= 0 || item.Size.Height <= 0)
            throw new InvalidOperationException("壁纸窗口尺寸无效。");
        using var device = CreateDevice();
        using var pool = Direct3D11CaptureFramePool.CreateFreeThreaded(device,
            DirectXPixelFormat.B8G8R8A8UIntNormalized, 1, item.Size);
        using var session = pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled = false;
        session.StartCapture();
        // Pull from a free-threaded pool: no UI activation, DispatcherQueue or
        // continuous capture subscription, and a bounded wait for paused wallpapers.
        var deadline = Environment.TickCount64 + 2500;
        while (Environment.TickCount64 < deadline)
        {
            using var frame = pool.TryGetNextFrame();
            if (frame is not null)
            {
                using var bitmap = await SoftwareBitmap.CreateCopyFromSurfaceAsync(frame.Surface);
                var pixels = new byte[checked(bitmap.PixelWidth * bitmap.PixelHeight * 4)];
                bitmap.CopyToBuffer(pixels.AsBuffer());
                return new(pixels, bitmap.PixelWidth, bitmap.PixelHeight);
            }
            await Task.Delay(25);
        }
        throw new TimeoutException("壁纸窗口未提供画面，请确认壁纸正在运行后重试。");
    }

    private static unsafe GraphicsCaptureItem CreateItem(IntPtr window)
    {
        const string name = "Windows.Graphics.Capture.GraphicsCaptureItem";
        Marshal.ThrowExceptionForHR(WindowsCreateString(name, name.Length, out var text));
        IntPtr factory = IntPtr.Zero, pointer = IntPtr.Zero;
        try
        {
            var interop = new Guid("3628e81b-3cac-4c60-b7f4-23ce0e0c3356");
            Marshal.ThrowExceptionForHR(RoGetActivationFactory(text, ref interop, out factory));
            var itemId = new Guid("79c3f95b-31f7-4ec2-a464-632ef5d30760");
            var create = (delegate* unmanaged[Stdcall]<IntPtr, IntPtr, Guid*, IntPtr*, int>)(*(IntPtr**)factory)[3];
            Marshal.ThrowExceptionForHR(create(factory, window, &itemId, &pointer));
            return WinRT.MarshalInterface<GraphicsCaptureItem>.FromAbi(pointer);
        }
        finally
        {
            if (pointer != IntPtr.Zero) Marshal.Release(pointer);
            if (factory != IntPtr.Zero) Marshal.Release(factory);
            WindowsDeleteString(text);
        }
    }

    private static IDirect3DDevice CreateDevice()
    {
        IntPtr native = IntPtr.Zero, context = IntPtr.Zero, dxgi = IntPtr.Zero, projected = IntPtr.Zero;
        try
        {
            int hr = D3D11CreateDevice(IntPtr.Zero, 1, IntPtr.Zero, 0x20, IntPtr.Zero, 0, 7,
                out native, out _, out context);
            if (hr < 0)
                hr = D3D11CreateDevice(IntPtr.Zero, 5, IntPtr.Zero, 0x20, IntPtr.Zero, 0, 7,
                    out native, out _, out context);
            Marshal.ThrowExceptionForHR(hr);
            var iid = new Guid("54ec77fa-1377-44e6-8c32-88fd5f44c84c");
            Marshal.ThrowExceptionForHR(Marshal.QueryInterface(native, ref iid, out dxgi));
            Marshal.ThrowExceptionForHR(CreateDirect3D11DeviceFromDXGIDevice(dxgi, out projected));
            return WinRT.MarshalInterface<IDirect3DDevice>.FromAbi(projected);
        }
        finally
        {
            if (projected != IntPtr.Zero) Marshal.Release(projected);
            if (dxgi != IntPtr.Zero) Marshal.Release(dxgi);
            if (context != IntPtr.Zero) Marshal.Release(context);
            if (native != IntPtr.Zero) Marshal.Release(native);
        }
    }

    [DllImport("combase.dll", CharSet = CharSet.Unicode)] private static extern int WindowsCreateString(string value, int length, out IntPtr text);
    [DllImport("combase.dll")] private static extern int WindowsDeleteString(IntPtr text);
    [DllImport("combase.dll")] private static extern int RoGetActivationFactory(IntPtr text, ref Guid iid, out IntPtr factory);
    [DllImport("d3d11.dll")] private static extern int D3D11CreateDevice(IntPtr adapter, int driverType, IntPtr software, uint flags, IntPtr levels, uint levelCount, uint version, out IntPtr device, out int level, out IntPtr context);
    [DllImport("d3d11.dll")] private static extern int CreateDirect3D11DeviceFromDXGIDevice(IntPtr dxgi, out IntPtr device);
}

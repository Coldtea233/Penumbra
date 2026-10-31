using Microsoft.UI.Xaml;

namespace Nocturne;
internal static class EntryPoint
{
    [STAThread]
    public static void Main(string[] args)
    {
        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(p =>
            {
                var queue = Microsoft.UI.Dispatching.DispatcherQueue.GetForCurrentThread();
                SynchronizationContext.SetSynchronizationContext(new Microsoft.UI.Dispatching.DispatcherQueueSynchronizationContext(queue));
                _ = new App();
            });
        }
        catch (Exception e)
        {
            Directory.CreateDirectory(Settings.DirectoryPath);
            File.WriteAllText(Path.Combine(Settings.DirectoryPath, "startup-error.txt"), e.ToString());
        }
    }
}

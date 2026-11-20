using Microsoft.Win32;
using System.Runtime.InteropServices;
using Windows.ApplicationModel;
using Windows.Services.Store;

namespace Nocturne;

internal sealed record StartupStatus(bool Enabled, bool CanChange, string? Message = null);

// Distribution changes installation integration, never the renderer's frame loop.
internal static class AppDistribution
{
#if PENUMBRA_STORE
    internal static readonly bool IsStore = true;
    internal const string ApplicationName = "沉浸式桌面";
    internal const string UpdateHelp = "通过 Microsoft Store 检查新版本；启动时自动检查一次。";
#else
    internal static readonly bool IsStore = false;
    internal const string ApplicationName = "Penumbra";
    internal const string UpdateHelp = "通过 GitHub Releases 检查新版本；启动时自动检查一次。";
#endif
    internal const string StartupTaskId = "PenumbraStartup";
    internal const string StoreId = "9PDRV35XC356";
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    internal static Uri StorePage => new("ms-windows-store://pdp/?ProductId=" + StoreId);

    internal static async Task<StartupStatus> ReadStartupAsync()
    {
        if (IsStore) return Describe((await StartupTask.GetAsync(StartupTaskId)).State);
        using var key = Registry.CurrentUser.OpenSubKey(RunKey);
        return new(key?.GetValue("Nocturne") is string, true);
    }

    internal static async Task<StartupStatus> SetStartupAsync(bool enabled, string rendererPath)
    {
        if (IsStore)
        {
            var task = await StartupTask.GetAsync(StartupTaskId);
            if (!enabled)
            {
                task.Disable();
                return Describe(task.State);
            }
            if (task.State == StartupTaskState.Disabled)
                return Describe(await task.RequestEnableAsync());
            return Describe(task.State);
        }
        using var key = Registry.CurrentUser.CreateSubKey(RunKey);
        if (enabled) key.SetValue("Nocturne", "\"" + rendererPath + "\"");
        else key.DeleteValue("Nocturne", false);
        return new(enabled, true);
    }

    private static StartupStatus Describe(StartupTaskState state) => state switch
    {
        StartupTaskState.Enabled => new(true, true),
        StartupTaskState.Disabled => new(false, true),
        StartupTaskState.EnabledByPolicy => new(true, false, "自启动由系统策略管理。"),
        StartupTaskState.DisabledByUser => new(false, false, "请到 Windows 设置 → 应用 → 启动中重新开启自启动。"),
        _ => new(false, false, "自启动已被系统策略禁用。")
    };

    internal static Task<UpdateResult> CheckForUpdatesAsync(IntPtr owner, string version, CancellationToken token)
        => IsStore ? CheckStoreAsync(owner, token) : UpdateChecker.CheckAsync(version, token);

    private static async Task<UpdateResult> CheckStoreAsync(IntPtr owner, CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromSeconds(12));
        try
        {
            // A local development package has no published Store entitlement.
            // Do not incorrectly report that an unpublished app is up to date.
            if (Package.Current.SignatureKind != PackageSignatureKind.Store)
                return new(UpdateState.Failed, "此为本地测试包；从 Microsoft Store 安装正式版后可检查商店更新。");
            var context = StoreContext.GetDefault();
            WinRT.Interop.InitializeWithWindow.Initialize(context, owner);
            var updates = await context.GetAppAndOptionalStorePackageUpdatesAsync().AsTask(deadline.Token);
            token.ThrowIfCancellationRequested();
            if (updates.Count != 0)
                return new(UpdateState.Available, "可由 Microsoft Store 下载并安装更新。", ReleasePage: StorePage, StoreInstallAvailable: true);
            return new(UpdateState.Current, "当前版本已为最新发行版");
        }
        catch (OperationCanceledException) when (!token.IsCancellationRequested)
        { return new(UpdateState.Failed, "连接 Microsoft Store 超时，请稍后重试。"); }
        catch (Exception ex) when (ex is COMException or InvalidOperationException or UnauthorizedAccessException)
        { return new(UpdateState.Failed, "暂时无法检查 Microsoft Store 更新，请确认商店可用后重试。"); }
    }

    internal static async Task<string> InstallStoreUpdateAsync(IntPtr owner, CancellationToken token)
    {
        if (!IsStore || Package.Current.SignatureKind != PackageSignatureKind.Store)
            return "此版本无法使用 Microsoft Store 应用内更新。";
        var context = StoreContext.GetDefault();
        WinRT.Interop.InitializeWithWindow.Initialize(context, owner);
        var updates = await context.GetAppAndOptionalStorePackageUpdatesAsync().AsTask(token);
        if (updates.Count == 0) return "当前版本已为最新发行版";
        var result = await context.RequestDownloadAndInstallStorePackageUpdatesAsync(updates).AsTask(token);
        return result.OverallState switch
        {
            StorePackageUpdateState.Completed => "Microsoft Store 已完成更新。",
            StorePackageUpdateState.Canceled => "已取消更新。",
            _ => "Microsoft Store 暂时未完成更新，请重试或选择查看更新。"
        };
    }
}

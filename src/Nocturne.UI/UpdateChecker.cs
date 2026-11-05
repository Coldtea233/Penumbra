using System.Net;
using System.Net.Http;
using System.Text.Json;

namespace Nocturne;

internal enum UpdateState { Available, Current, Failed }
internal sealed record UpdateResult(UpdateState State, string Message, string? Version = null, Uri? ReleasePage = null);

// Runs only in the settings process, once at startup or on an explicit click.
// No token, background timer, automatic download, or native-renderer dependency.
internal static class UpdateChecker
{
    internal const string Endpoint = "https://api.github.com/repos/Coldtea233/Penumbra/releases/latest";
    internal const string LatestPage = "https://github.com/Coldtea233/Penumbra/releases/latest";
    internal const int MaximumResponseBytes = 512 * 1024;

    internal static string DisplayVersion(string informationalVersion)
    {
        string version = informationalVersion.Split('+')[0].Trim();
        return version.StartsWith('v') || version.StartsWith('V') ? version[1..] : version;
    }

    private static bool TryVersion(string text, out Version version)
    {
        version = new Version(0, 0, 0);
        string normalized = DisplayVersion(text);
        string[] parts = normalized.Split('.');
        if (parts.Length != 3 || parts.Any(p => p.Length == 0 || p.Length > 10 ||
            p.Any(c => c is < '0' or > '9') || (p.Length > 1 && p[0] == '0')))
            return false;
        if (!System.Version.TryParse(normalized, out var parsed)) return false;
        version = parsed;
        return true;
    }

    internal static Task<UpdateResult> CheckAsync(string currentVersion, CancellationToken cancellationToken)
        => CheckWithOwnedClientAsync(currentVersion, cancellationToken);

    private static async Task<UpdateResult> CheckWithOwnedClientAsync(string currentVersion, CancellationToken cancellationToken)
    {
        using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false })
        { Timeout = Timeout.InfiniteTimeSpan };
        return await CheckAsync(client, currentVersion, cancellationToken).ConfigureAwait(false);
    }

    // Injected client/timeout allow the actual production path to be tested
    // against offline HTTP fixtures without modifying the GitHub repository.
    internal static async Task<UpdateResult> CheckAsync(HttpClient client, string currentVersion,
        CancellationToken cancellationToken, TimeSpan? timeout = null)
    {
        if (!TryVersion(currentVersion, out var current))
            return new(UpdateState.Failed, "无法识别当前版本号。");
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        deadline.CancelAfter(timeout ?? TimeSpan.FromSeconds(8));
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, Endpoint);
            request.Headers.UserAgent.ParseAdd("Penumbra/" + current);
            request.Headers.Accept.ParseAdd("application/vnd.github+json");
            request.Headers.Add("X-GitHub-Api-Version", "2022-11-28");
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, deadline.Token).ConfigureAwait(false);
            if (response.StatusCode is HttpStatusCode.Forbidden or HttpStatusCode.TooManyRequests)
            {
                var fallback = await CheckReleaseLinkAsync(client, current, deadline.Token).ConfigureAwait(false);
                cancellationToken.ThrowIfCancellationRequested();
                if (fallback is not null) return fallback;
                return new(UpdateState.Failed, "GitHub 暂时拒绝请求或检查次数已达上限，请稍后重试。");
            }
            if (response.StatusCode == HttpStatusCode.NotFound)
                return new(UpdateState.Failed, "暂未找到公开的正式发行版，请稍后重试。");
            if (!response.IsSuccessStatusCode)
            {
                if ((int)response.StatusCode >= 500)
                {
                    var fallback = await CheckReleaseLinkAsync(client, current, deadline.Token).ConfigureAwait(false);
                    cancellationToken.ThrowIfCancellationRequested();
                    if (fallback is not null) return fallback;
                }
                return new(UpdateState.Failed, $"GitHub 暂时不可用（HTTP {(int)response.StatusCode}），请稍后重试。");
            }
            if (response.Content.Headers.ContentLength > MaximumResponseBytes)
                return new(UpdateState.Failed, "更新信息过大，已安全取消本次检查。");
            using var body = await response.Content.ReadAsStreamAsync(deadline.Token).ConfigureAwait(false);
            using var buffer = new MemoryStream();
            byte[] chunk = new byte[8192];
            int read;
            while ((read = await body.ReadAsync(chunk, deadline.Token).ConfigureAwait(false)) != 0)
            {
                if (buffer.Length + read > MaximumResponseBytes)
                    return new(UpdateState.Failed, "更新信息过大，已安全取消本次检查。");
                buffer.Write(chunk, 0, read);
            }
            using var json = JsonDocument.Parse(buffer.GetBuffer().AsMemory(0, (int)buffer.Length));
            var release = json.RootElement;
            if (release.ValueKind != JsonValueKind.Object ||
                !release.TryGetProperty("draft", out var draft) || draft.ValueKind != JsonValueKind.False ||
                !release.TryGetProperty("prerelease", out var prerelease) || prerelease.ValueKind != JsonValueKind.False ||
                !release.TryGetProperty("tag_name", out var tagProperty) || tagProperty.ValueKind != JsonValueKind.String)
                return new(UpdateState.Failed, "GitHub 返回的正式版本信息无效，请稍后重试。");
            string tag = tagProperty.GetString()!;
            if (tag.Length > 64 || tag != tag.Trim() || !TryVersion(tag, out var latest))
                return new(UpdateState.Failed, "无法识别 GitHub 发行版的版本号。");
            return Compare(current, latest, tag);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        { return new(UpdateState.Failed, "连接 GitHub 超时，请检查网络后重试。"); }
        catch (HttpRequestException)
        {
            var fallback = await CheckReleaseLinkAsync(client, current, deadline.Token).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            return fallback ?? new(UpdateState.Failed, "无法连接 GitHub，请检查网络后重试。");
        }
        catch (IOException)
        { return new(UpdateState.Failed, "读取更新信息失败，请稍后重试。"); }
        catch (JsonException)
        { return new(UpdateState.Failed, "GitHub 返回的更新信息无法解析，请稍后重试。"); }
    }

    private static UpdateResult Compare(Version current, Version latest, string tag)
    {
        // Never open an arbitrary URL supplied by a response or asset.
        var page = new Uri("https://github.com/Coldtea233/Penumbra/releases/tag/" + Uri.EscapeDataString(tag));
        if (latest > current)
            return new(UpdateState.Available, "可前往 GitHub 查看更新并下载安装包。更新前请从托盘退出旧版。", latest.ToString(), page);
        return new(UpdateState.Current, latest == current ? "当前版本已为最新发行版" : "当前版本高于 GitHub 最新正式发行版。", latest.ToString());
    }

    // Public GitHub latest-release redirect is a low-cost fallback when a
    // shared IP exhausts the anonymous API quota. HEAD only: no HTML scraping,
    // third-party mirror, token, installer download, or automatic redirect.
    private static async Task<UpdateResult?> CheckReleaseLinkAsync(HttpClient client, Version current, CancellationToken token)
    {
        try
        {
            using var request = new HttpRequestMessage(HttpMethod.Head, LatestPage);
            request.Headers.UserAgent.ParseAdd("Penumbra/" + current);
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token).ConfigureAwait(false);
            if (response.StatusCode is not (HttpStatusCode.MovedPermanently or HttpStatusCode.Found or
                HttpStatusCode.SeeOther or HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect) ||
                response.Headers.Location is not Uri location) return null;
            Uri page = location.IsAbsoluteUri ? location : new Uri(new Uri(LatestPage), location);
            const string prefix = "/Coldtea233/Penumbra/releases/tag/";
            if (page.Scheme != Uri.UriSchemeHttps || page.Host != "github.com" || page.Port != 443 ||
                page.UserInfo.Length != 0 || page.Query.Length != 0 || page.Fragment.Length != 0 ||
                !page.AbsolutePath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) return null;
            string tag = Uri.UnescapeDataString(page.AbsolutePath[prefix.Length..]);
            if (tag.Length > 64 || tag != tag.Trim() || !TryVersion(tag, out var latest)) return null;
            return Compare(current, latest, tag);
        }
        catch (Exception error) when (error is HttpRequestException or IOException or OperationCanceledException or UriFormatException)
        { return null; }
    }
}

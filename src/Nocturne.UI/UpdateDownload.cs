using System.Net;
using System.Security.Cryptography;

namespace Nocturne;

internal static class UpdateDownload
{
    internal static async Task<string> DownloadAsync(UpdateAsset asset, IProgress<int> progress, CancellationToken token)
    {
        using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = Timeout.InfiniteTimeSpan };
        return await DownloadAsync(client, asset, progress, token).ConfigureAwait(false);
    }

    internal static async Task<string> DownloadAsync(HttpClient client, UpdateAsset asset, IProgress<int> progress, CancellationToken token)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(token);
        deadline.CancelAfter(TimeSpan.FromMinutes(5));
        var cancellation = deadline.Token;
        using var response = await GetReleaseAssetAsync(client, asset.Url, cancellation).ConfigureAwait(false);
        response.EnsureSuccessStatusCode();
        long? declaredSize = response.Content.Headers.ContentLength;
        if (declaredSize is < 1 or > UpdateAsset.MaximumBytes ||
            (asset.Size.HasValue && declaredSize.HasValue && declaredSize != asset.Size))
            throw new InvalidDataException("安装包大小与发行信息不符。");
        long? expectedSize = asset.Size ?? declaredSize;
        long limit = expectedSize ?? UpdateAsset.MaximumBytes;
        var directory = Directory.CreateTempSubdirectory("Penumbra-update-");
        string path = Path.Combine(directory.FullName, asset.Name);
        bool keep = false;
        try
        {
            using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
            await using var body = await response.Content.ReadAsStreamAsync(cancellation).ConfigureAwait(false);
            await using var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 65536, useAsync: true);
            byte[] buffer = new byte[65536];
            long total = 0;
            int lastPercent = -1;
            int count;
            while ((count = await body.ReadAsync(buffer, cancellation).ConfigureAwait(false)) != 0)
            {
                total += count;
                if (total > limit) throw new InvalidDataException("安装包超过下载大小限制。");
                hash.AppendData(buffer, 0, count);
                await file.WriteAsync(buffer.AsMemory(0, count), cancellation).ConfigureAwait(false);
                if (expectedSize is long size)
                {
                    int percent = (int)(100 * total / size);
                    if (percent != lastPercent) progress.Report(lastPercent = percent);
                }
            }
            if (total == 0 || (expectedSize.HasValue && total != expectedSize) ||
                !string.Equals(Convert.ToHexString(hash.GetHashAndReset()), asset.Sha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("安装包 SHA-256 校验失败。");
            await file.FlushAsync(cancellation).ConfigureAwait(false);
            keep = true;
            return path;
        }
        finally
        {
            if (!keep) Cleanup(path);
        }
    }

    internal static async Task<HttpResponseMessage> GetReleaseAssetAsync(HttpClient client, Uri url, CancellationToken token)
    {
        for (int redirect = 0; redirect <= 3; redirect++)
        {
            if (url.Scheme != Uri.UriSchemeHttps || url.Port != 443 || url.UserInfo.Length != 0 ||
                (redirect == 0 ? url.Host != "github.com" : url.Host is not ("release-assets.githubusercontent.com" or "objects.githubusercontent.com")))
                throw new InvalidDataException("发行文件下载地址不受信任。");
            using var request = new HttpRequestMessage(HttpMethod.Get, url);
            request.Headers.UserAgent.ParseAdd("Penumbra-Updater/1.0");
            var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token).ConfigureAwait(false);
            if (response.StatusCode is not (HttpStatusCode.MovedPermanently or HttpStatusCode.Found or HttpStatusCode.SeeOther or
                HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect)) return response;
            var location = response.Headers.Location;
            response.Dispose();
            if (location is null) break;
            url = new Uri(url, location);
        }
        throw new InvalidDataException("无法解析发行文件的下载地址。");
    }

    internal static void Cleanup(string path)
    {
        try
        {
            File.Delete(path);
            Directory.Delete(Path.GetDirectoryName(path)!);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }
}

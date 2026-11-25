using System.Diagnostics;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using Nocturne;

int checks = 0, failures = 0;
void Check(bool condition, string description)
{
    checks++;
    if (!condition) { failures++; Console.WriteLine("FAIL " + description); }
}
string Release(string version, bool draft = false, bool prerelease = false) =>
    "{\"tag_name\":\"" + version + "\",\"draft\":" + draft.ToString().ToLowerInvariant() +
    ",\"prerelease\":" + prerelease.ToString().ToLowerInvariant() + "}";
async Task<UpdateResult> Fixture(string body, HttpStatusCode status = HttpStatusCode.OK, string current = "1.8.1")
{
    using var handler = new FixtureHandler((request, _) =>
    {
        bool api = request.Method == HttpMethod.Get;
        Check((api || request.Method == HttpMethod.Head) && request.RequestUri?.AbsoluteUri == (api ? UpdateChecker.Endpoint : UpdateChecker.LatestPage), "fixed read-only release endpoints");
        Check(request.Headers.Authorization is null, "no credentials sent");
        Check(request.Headers.UserAgent.ToString() == "Penumbra/" + UpdateChecker.DisplayVersion(current), "versioned User-Agent");
        Check(!api || (request.Headers.Accept.ToString() == "application/vnd.github+json" &&
            request.Headers.GetValues("X-GitHub-Api-Version").Single() == "2022-11-28"), "GitHub API headers");
        return Task.FromResult(new HttpResponseMessage(status) { Content = new StringContent(body, Encoding.UTF8, "application/json") });
    });
    using var client = new HttpClient(handler);
    var result = await UpdateChecker.CheckAsync(client, current, CancellationToken.None);
    bool fallback = status is HttpStatusCode.Forbidden or HttpStatusCode.TooManyRequests || (int)status >= 500;
    Check(handler.Requests == (fallback ? 2 : 1), "at most one API request and one bounded fallback, no retry/poll");
    return result;
}

foreach (var (input, expected) in new[] { ("v1.8.1+commit", "1.8.1"), ("1.8.1", "1.8.1"), ("V1.8.1", "1.8.1"), ("1.8.1+hash", "1.8.1") })
    Check(UpdateChecker.DisplayVersion(input) == expected, "comparison version omits prefix and build metadata");
foreach (string version in new[] { "v1.8.2", "1.8.2", "v1.9.0", "v1.10.0", "v2.0.0", "v10.0.0", "v1.8.2+build.1" })
{
    var result = await Fixture(Release(version));
    Check(result.State == UpdateState.Available, "newer semantic version " + version);
    Check(result.ReleasePage?.AbsoluteUri == "https://github.com/Coldtea233/Penumbra/releases/tag/" + Uri.EscapeDataString(version), "safe release page for " + version);
    Check(result.Version == UpdateChecker.DisplayVersion(version), "new version label without v");
}
foreach (string version in new[] { "v1.8.1", "1.8.1", "V1.8.1", "v1.8.1+build", "v1.8.0", "v1.7.99", "v0.99.99" })
{
    var result = await Fixture(Release(version));
    Check(result.State == UpdateState.Current && result.ReleasePage is null, "equal/older version never prompts downgrade " + version);
    if (UpdateChecker.DisplayVersion(version) == "1.8.1")
        Check(result.Message == "当前版本已为最新发行版", "latest release uses the requested status wording");
}
Check((await Fixture(Release("v1.8.1"), current: "v1.8.1")).Message == "当前版本已为最新发行版", "displayed v prefix still compares correctly");
Check((await Fixture(Release("v1.9.0"), current: "1.10.0")).State == UpdateState.Current, "numeric, not lexical comparison");
foreach (string version in new[] { "", "v", "1.8", "1.8.2.0", "1.8.2-beta.1", "1.8.-1", "01.8.2", "1..2", "999999999999.0.0", "1.8.2/../../evil", "https://example.org", " v1.8.2", "v1.8.2 " })
    Check((await Fixture(Release(version))).State == UpdateState.Failed, "invalid tag safely rejected " + version);
Check((await Fixture(Release("v9.0.0", draft: true))).State == UpdateState.Failed, "drafts never offered");
Check((await Fixture(Release("v9.0.0", prerelease: true))).State == UpdateState.Failed, "prereleases never offered");
foreach (string body in new[] { "", "<html>proxy error</html>", "{", "[]", "null", "{}", "{\"tag_name\":42,\"draft\":false,\"prerelease\":false}", "{\"tag_name\":\"v9.0.0\",\"draft\":\"false\",\"prerelease\":false}" })
    Check((await Fixture(body)).State == UpdateState.Failed, "invalid response handled");
foreach (var status in new[] { HttpStatusCode.Forbidden, HttpStatusCode.TooManyRequests, HttpStatusCode.NotFound, HttpStatusCode.InternalServerError, HttpStatusCode.Found })
    Check((await Fixture("{}", status)).State == UpdateState.Failed, "HTTP failure permits retry " + status);
var hostileUrl = Release("v1.8.2")[..^1] + ",\"html_url\":\"https://evil.invalid/run.exe\"}";
Check((await Fixture(hostileUrl)).ReleasePage?.Host == "github.com", "response URLs cannot redirect download action");

foreach (string location in new[] { "/Coldtea233/Penumbra/releases/tag/v1.8.2", "https://github.com/Coldtea233/Penumbra/releases/tag/v1.8.2", "https://evil.invalid/Coldtea233/Penumbra/releases/tag/v9.0.0", "http://github.com/Coldtea233/Penumbra/releases/tag/v9.0.0", "https://github.com:8443/Coldtea233/Penumbra/releases/tag/v9.0.0", "/Other/Repository/releases/tag/v9.0.0", "/Coldtea233/Penumbra/releases/tag/v9.0.0-beta.1", "/Coldtea233/Penumbra/releases/tag/v9.0.0?redirect=evil", "/Coldtea233/Penumbra/releases/tag/v9.0.0%2Fmalicious" })
{
    using var handler = new FixtureHandler((request, _) =>
    {
        if (request.Method == HttpMethod.Get) return Task.FromResult(new HttpResponseMessage(HttpStatusCode.Forbidden));
        Check(request.Method == HttpMethod.Head && request.RequestUri?.AbsoluteUri == UpdateChecker.LatestPage, "fallback uses HEAD without fetching HTML");
        var response = new HttpResponseMessage(HttpStatusCode.Found);
        response.Headers.Location = new Uri(location, UriKind.RelativeOrAbsolute);
        return Task.FromResult(response);
    });
    using var client = new HttpClient(handler);
    var result = await UpdateChecker.CheckAsync(client, "1.8.1", CancellationToken.None);
    bool valid = location is "/Coldtea233/Penumbra/releases/tag/v1.8.2" or "https://github.com/Coldtea233/Penumbra/releases/tag/v1.8.2";
    Check(result.State == (valid ? UpdateState.Available : UpdateState.Failed) && result.Asset is null &&
        handler.Requests == (valid ? 3 : 2), "fallback validates release host/path/version " + location);
}

using (var handler = new FixtureHandler((_, _) => throw new HttpRequestException("offline")))
using (var client = new HttpClient(handler))
    Check((await UpdateChecker.CheckAsync(client, "1.8.1", CancellationToken.None)).State == UpdateState.Failed, "network exceptions become retryable status");
using (var handler = new FixtureHandler(async (_, token) => { await Task.Delay(Timeout.Infinite, token); return new HttpResponseMessage(); }))
using (var client = new HttpClient(handler))
{
    var clock = Stopwatch.StartNew();
    var result = await UpdateChecker.CheckAsync(client, "1.8.1", CancellationToken.None, TimeSpan.FromMilliseconds(30));
    Check(result.State == UpdateState.Failed && result.Message.Contains("超时") && clock.ElapsedMilliseconds < 2000, "bounded timeout without blocking UI");
}
using (var handler = new FixtureHandler(async (_, token) => { await Task.Delay(Timeout.Infinite, token); return new HttpResponseMessage(); }))
using (var client = new HttpClient(handler))
using (var cancellation = new CancellationTokenSource(30))
{
    bool canceled = false;
    try { await UpdateChecker.CheckAsync(client, "1.8.1", cancellation.Token); }
    catch (OperationCanceledException) { canceled = true; }
    Check(canceled, "closing settings cancels outstanding HTTP work");
}
foreach (bool declaredLength in new[] { false, true })
{
    using var handler = new FixtureHandler((_, _) =>
    {
        HttpContent content = declaredLength ? new ByteArrayContent([]) : new StreamingContent(new byte[UpdateChecker.MaximumResponseBytes + 1]);
        if (declaredLength) content.Headers.ContentLength = UpdateChecker.MaximumResponseBytes + 1;
        return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = content });
    });
    using var client = new HttpClient(handler);
    Check((await UpdateChecker.CheckAsync(client, "1.8.1", CancellationToken.None)).State == UpdateState.Failed, "response size capped with/without Content-Length");
}
using (var handler = new FixtureHandler((_, _) => throw new Exception("invalid current version must not request network")))
using (var client = new HttpClient(handler))
{
    Check((await UpdateChecker.CheckAsync(client, "bad", CancellationToken.None)).State == UpdateState.Failed && handler.Requests == 0,
        "invalid local version fails before HTTP");
}
byte[] fixtureInstaller = [13, 22, 35];
string installerDigest = Convert.ToHexString(SHA256.HashData(fixtureInstaller));
var withInstaller = Release("v1.9.2")[..^1] +
    ",\"assets\":[{\"name\":\"Penumbra-v1.9.2-Setup-x64.exe\",\"state\":\"uploaded\",\"size\":3,\"digest\":\"sha256:" + installerDigest +
    "\",\"browser_download_url\":\"https://evil.invalid/update.exe\"}]}";
var installerFixture = await Fixture(withInstaller);
Check(installerFixture.Asset?.Url.AbsoluteUri == "https://github.com/Coldtea233/Penumbra/releases/download/v1.9.2/Penumbra-v1.9.2-Setup-x64.exe", "trusted asset download URL is derived from the tag");
Check((await Fixture(withInstaller.Replace("sha256:", "sha1:"))).Asset is null, "missing SHA-256 leaves manual release link only");
Check((await Fixture(withInstaller, current: "1.9.2")).Asset is null, "same version never offered in-app");
const string checksumUrl = "https://github.com/Coldtea233/Penumbra/releases/download/v1.9.2/v1.9.2.SHA256SUMS.txt";
const string checksumCdnUrl = "https://release-assets.githubusercontent.com/checksum";
const string installerName = "Penumbra-v1.9.2-Setup-x64.exe";
string checksum = installerDigest + "  " + installerName + "\r\n";
foreach (var (status, current, scenario) in new[]
{
    (HttpStatusCode.Forbidden, "1.9.1", "verified"),
    (HttpStatusCode.TooManyRequests, "1.9.1", "verified"),
    (HttpStatusCode.Forbidden, "1.9.2", "current"),
    (HttpStatusCode.Forbidden, "1.9.1", "missing"),
    (HttpStatusCode.Forbidden, "1.9.1", "wrong filename"),
    (HttpStatusCode.Forbidden, "1.9.1", "malformed hash"),
    (HttpStatusCode.Forbidden, "1.9.1", "duplicate"),
    (HttpStatusCode.Forbidden, "1.9.1", "oversized stream"),
    (HttpStatusCode.Forbidden, "1.9.1", "untrusted redirect"),
    (HttpStatusCode.Forbidden, "1.9.1", "timed out")
})
{
    using var handler = new FixtureHandler((request, _) =>
    {
        Check(request.Headers.Authorization is null, "no credentials on the fallback path");
        var response = request.RequestUri!.AbsoluteUri switch
        {
            UpdateChecker.Endpoint => new HttpResponseMessage(status),
            UpdateChecker.LatestPage when request.Method == HttpMethod.Head => new HttpResponseMessage(HttpStatusCode.Found)
            {
                Headers = { Location = new Uri("/Coldtea233/Penumbra/releases/tag/v1.9.2", UriKind.Relative) }
            },
            checksumUrl when scenario == "missing" => new HttpResponseMessage(HttpStatusCode.NotFound),
            checksumUrl when scenario == "timed out" => throw new OperationCanceledException(),
            checksumUrl => new HttpResponseMessage(HttpStatusCode.Found)
            {
                Headers = { Location = new Uri(scenario == "untrusted redirect" ? "https://evil.invalid/checksum" : checksumCdnUrl) }
            },
            checksumCdnUrl => new HttpResponseMessage(HttpStatusCode.OK)
            {
                Content = scenario switch
                {
                    "wrong filename" => new StringContent(checksum.Replace(installerName, "other.exe")),
                    "malformed hash" => new StringContent("g" + checksum[1..]),
                    "duplicate" => new StringContent(checksum + checksum),
                    "oversized stream" => new StreamingContent(Encoding.UTF8.GetBytes(checksum + new string(' ', 20000))),
                    _ => new StringContent(checksum)
                }
            },
            _ => throw new InvalidOperationException("Update check must not request the installer or an unknown host.")
        };
        return Task.FromResult(response);
    });
    using var client = new HttpClient(handler);
    var result = await UpdateChecker.CheckAsync(client, current, CancellationToken.None);
    if (scenario == "current")
        Check(result.State == UpdateState.Current && result.Asset is null && handler.Requests == 2,
            "no checksum request when already up to date");
    else if (scenario == "verified")
        Check(result.State == UpdateState.Available && result.Asset is { Size: null } asset &&
            asset.Sha256 == installerDigest && asset.Url == installerFixture.Asset?.Url && handler.Requests == 4,
            "in-app installation available after API " + status);
    else
        Check(result.State == UpdateState.Available && result.Asset is null && result.ReleasePage is not null,
            "manual release page retained for checksum: " + scenario);
}

foreach (string scenario in new[] { "verified", "wrong hash", "untrusted redirect" })
{
    var installer = installerFixture.Asset!;
    using var handler = new FixtureHandler((request, _) =>
    {
        if (request.RequestUri?.Host == "github.com")
        {
            var redirect = new HttpResponseMessage(HttpStatusCode.Found);
            redirect.Headers.Location = new Uri(scenario == "untrusted redirect" ? "https://evil.invalid/asset" : "https://release-assets.githubusercontent.com/asset");
            return Task.FromResult(redirect);
        }
        byte[] bytes = scenario == "wrong hash" ? [1, 2, 3] : fixtureInstaller;
        return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) });
    });
    using var client = new HttpClient(handler);
    try
    {
        string path = await UpdateDownload.DownloadAsync(client, installer, new Progress<int>(), CancellationToken.None);
        try { Check(scenario == "verified" && File.ReadAllBytes(path).SequenceEqual(fixtureInstaller), "download returns a file only after verification"); }
        finally { UpdateDownload.Cleanup(path); }
    }
    catch (InvalidDataException) { Check(scenario != "verified" && handler.Requests == (scenario == "untrusted redirect" ? 1 : 2), "blocks " + scenario); }
}

foreach (string scenario in new[] { "length unknown", "oversized length", "wrong hash with length unknown" })
{
    var asset = installerFixture.Asset! with { Size = null };
    using var handler = new FixtureHandler((_, _) =>
    {
        HttpContent content = scenario == "wrong hash with length unknown" ? new StreamingContent([1, 2, 3]) : new StreamingContent(fixtureInstaller);
        if (scenario == "oversized length") content.Headers.ContentLength = UpdateAsset.MaximumBytes + 1;
        return Task.FromResult(new HttpResponseMessage(HttpStatusCode.OK) { Content = content });
    });
    using var client = new HttpClient(handler);
    try
    {
        string path = await UpdateDownload.DownloadAsync(client, asset, new Progress<int>(), CancellationToken.None);
        try { Check(scenario == "length unknown" && File.ReadAllBytes(path).SequenceEqual(fixtureInstaller), "fallback verifies hash even when length is unknown"); }
        finally { UpdateDownload.Cleanup(path); }
    }
    catch (InvalidDataException) { Check(scenario != "length unknown", "fallback download blocks " + scenario); }
}

if (args.Contains("--live"))
{
    var clock = Stopwatch.StartNew();
    var result = await UpdateChecker.CheckAsync("1.9.1", CancellationToken.None);
    Check(result.State != UpdateState.Failed, "real unauthenticated GitHub release request: " + result.Message);
    Console.WriteLine($"LiveGitHub: state={result.State}; release={result.Version}; inAppInstall={result.Asset is not null}; elapsedMs={clock.ElapsedMilliseconds}; token=false");
    if (args.Contains("--download"))
    {
        Check(result.Asset is not null, "the public release offers in-app installation");
        if (result.Asset is { } asset)
        {
            using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(45));
            string path = await UpdateDownload.DownloadAsync(asset, new Progress<int>(), deadline.Token);
            try { Console.WriteLine($"LiveDownload: bytes={new FileInfo(path).Length}; sha256Verified=true; executed=false"); }
            finally { UpdateDownload.Cleanup(path); }
        }
    }
}
Console.WriteLine($"{{\"UpdateChecks\":{checks},\"Failures\":{failures},\"SettingsWritten\":false,\"RepositoryModified\":false}}");
return failures == 0 ? 0 : 1;

sealed class FixtureHandler(Func<HttpRequestMessage, CancellationToken, Task<HttpResponseMessage>> fixture) : HttpMessageHandler
{
    internal int Requests { get; private set; }
    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
    { Requests++; return fixture(request, token); }
}
sealed class StreamingContent(byte[] data) : HttpContent
{
    protected override bool TryComputeLength(out long length) { length = 0; return false; }
    protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) => stream.WriteAsync(data).AsTask();
}

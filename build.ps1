param([string]$Toolchain = $env:LLVM_MINGW_ROOT, [string]$Configuration = 'Release', [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist\Penumbra-v1.8.1'), [switch]$NoRestore, [switch]$SkipStartupCheck)
$ErrorActionPreference = 'Stop'
if (-not $Toolchain) { throw '请通过 -Toolchain 指定 llvm-mingw 工具链目录，或设置 LLVM_MINGW_ROOT。' }
$Toolchain = (Resolve-Path -LiteralPath $Toolchain).Path
$projectRoot = $PSScriptRoot
$dist = if ([IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory } else { Join-Path $projectRoot $OutputDirectory }
New-Item -ItemType Directory -Force $dist | Out-Null
$appDirectory = Join-Path $dist 'app'
$docsDirectory = Join-Path $dist 'docs'
New-Item -ItemType Directory -Force $appDirectory, $docsDirectory | Out-Null
Push-Location (Join-Path $projectRoot 'src\Penumbra.Launcher')
try {
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-windres.exe') launcher.rc -O coff -o launcher.res
    if ($LASTEXITCODE) { throw '启动器资源编译失败' }
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') main.cpp launcher.res -std=c++17 -Os -Wall -Wextra -static -s -municode -mwindows -luser32 -o (Join-Path $dist 'Penumbra.exe')
    if ($LASTEXITCODE) { throw '启动器编译失败' }
    Remove-Item -LiteralPath launcher.res
} finally { Pop-Location }
Push-Location (Join-Path $projectRoot 'src\Nocturne.Renderer')
try {
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-windres.exe') renderer.rc -O coff -o renderer.res
    if ($LASTEXITCODE) { throw '资源编译失败' }
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') main.cpp renderer.res -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -s -municode -mwindows -luser32 -lgdi32 -lshell32 -lole32 -luuid -ldwmapi -lruntimeobject -lcoremessaging -ladvapi32 -o (Join-Path $appDirectory 'Nocturne.Renderer.exe')
    if ($LASTEXITCODE) { throw '原生程序编译失败' }
    Remove-Item -LiteralPath renderer.res
} finally { Pop-Location }
$penumbraPreviousGcServer=$env:DOTNET_gcServer
$env:DOTNET_gcServer='0'
try {
    # WinUI + ILLink incremental output can retain an old .deps.json even
    # after new assemblies were copied. Publish one clean, consistent closure.
    & dotnet clean (Join-Path $projectRoot 'src\Nocturne.UI\Nocturne.UI.csproj') -c $Configuration -p:Platform=x64 -p:RuntimeIdentifier=win-x64 -v:quiet
    if ($LASTEXITCODE) { throw '清理 WinUI 编译缓存失败' }
    [string[]]$restoreOptions=if($NoRestore){@('--no-restore')}else{@()}
    & dotnet publish (Join-Path $projectRoot 'src\Nocturne.UI\Nocturne.UI.csproj') -c $Configuration -p:Platform=x64 -p:UseSharedCompilation=false -p:BuildInParallel=false -m:1 -o $appDirectory @restoreOptions
    if ($LASTEXITCODE) { throw 'WinUI 3 编译失败' }
} finally { $env:DOTNET_gcServer=$penumbraPreviousGcServer }
$test = Start-Process (Join-Path $appDirectory 'Nocturne.Renderer.exe') -ArgumentList '--self-test' -PassThru -Wait -WindowStyle Hidden
if ($test.ExitCode) { throw "暗角公式测试失败: $($test.ExitCode)" }
if (!$SkipStartupCheck) {
    $testDirectory=Join-Path $projectRoot 'tests\bin'
    New-Item -ItemType Directory -Force -Path $testDirectory | Out-Null
    $startupReport=Join-Path $testDirectory ('PublishedStartup-'+[Guid]::NewGuid().ToString('N')+'.txt')
    $startup=Start-Process -FilePath (Join-Path $appDirectory 'Penumbra.exe') -ArgumentList ('"--startup-check='+$startupReport+'"') -PassThru -WindowStyle Hidden
    if (!$startup.WaitForExit(15000)) { throw "界面启动检查超时：$startupReport" }
    if ($startup.ExitCode -or !(Test-Path -LiteralPath $startupReport) -or (Get-Content -LiteralPath $startupReport -First 1) -ne 'PASS') {
        if (Test-Path -LiteralPath $startupReport) { Get-Content -LiteralPath $startupReport | Write-Host }
        throw "发布包界面启动检查失败：$startupReport"
    }
    Write-Host '发布包 WinUI 启动检查通过。'
} else { Write-Warning '已跳过发布包 WinUI 启动检查；此构建尚未验证可启动。' }
Copy-Item (Join-Path $projectRoot 'README.md') $dist
Copy-Item (Join-Path $projectRoot 'TEST-REPORT.md') $docsDirectory
Copy-Item (Join-Path $projectRoot 'RELEASE-NOTES.md') $docsDirectory
Copy-Item (Join-Path $projectRoot 'THIRD-PARTY-NOTICES.txt') $docsDirectory
Write-Host "构建完成：$dist"

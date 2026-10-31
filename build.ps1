param([string]$Toolchain = $env:LLVM_MINGW_ROOT, [string]$Configuration = 'Release', [string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist\Penumbra-beta-0.1-r3'))
$ErrorActionPreference = 'Stop'
if (-not $Toolchain) { throw '请通过 -Toolchain 指定 llvm-mingw 工具链目录，或设置 LLVM_MINGW_ROOT。' }
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
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') main.cpp renderer.res -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -s -municode -mwindows -luser32 -lgdi32 -lshell32 -lole32 -luuid -o (Join-Path $appDirectory 'Nocturne.Renderer.exe')
    if ($LASTEXITCODE) { throw '原生程序编译失败' }
    Remove-Item -LiteralPath renderer.res
} finally { Pop-Location }
& dotnet publish (Join-Path $projectRoot 'src\Nocturne.UI\Nocturne.UI.csproj') -c $Configuration -p:Platform=x64 -o $appDirectory
if ($LASTEXITCODE) { throw 'WinUI 3 编译失败' }
$test = Start-Process (Join-Path $appDirectory 'Nocturne.Renderer.exe') -ArgumentList '--self-test' -PassThru -Wait -WindowStyle Hidden
if ($test.ExitCode) { throw "暗角公式测试失败: $($test.ExitCode)" }
Copy-Item (Join-Path $projectRoot 'README.md') $dist
Copy-Item (Join-Path $projectRoot 'TEST-REPORT.md') $docsDirectory
Copy-Item (Join-Path $projectRoot 'THIRD-PARTY-NOTICES.txt') $docsDirectory
Write-Host "构建完成：$dist"

param([string]$Toolchain=$env:LLVM_MINGW_ROOT,[switch]$WindowEvents,[switch]$VisualProbe,[switch]$Blur,[switch]$Branding,[string]$BrandingExecutable)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw directory.'}
$testDirectory=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $testDirectory|Out-Null
$testNames=@('DesktopLayeringTests','RendererTests')
if($WindowEvents){$testNames+='WindowPresenceTests'}
if($VisualProbe){$testNames+='LayeredOpacityTests'}
if($Blur){$testNames+=@('BlurStateTests','BlurProbe')}
if($Branding){
    if(!$BrandingExecutable){throw 'Pass -BrandingExecutable with the built Nocturne.Renderer.exe.'}
    $BrandingExecutable=(Resolve-Path -LiteralPath $BrandingExecutable).Path
    $testNames+='BrandingTests'
}
foreach($testName in $testNames){
    $testExecutable=Join-Path $testDirectory "$testName.exe"
    [string[]]$entryOptions=if($testName -eq 'BrandingTests'){@('-municode')}else{@()}
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot "$testName.cpp") -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static @entryOptions -luser32 -lgdi32 -lshell32 -lole32 -luuid -lpsapi -ldwmapi -lruntimeobject -lcoremessaging -ladvapi32 -o $testExecutable
    if($LASTEXITCODE){throw "Native test compilation failed: $testName"}
    if($testName -eq 'BrandingTests'){& $testExecutable $BrandingExecutable}else{& $testExecutable}
    if($LASTEXITCODE -eq 77){Write-Warning "Visual fixture unavailable: $testName was NOT verified.";continue}
    if($LASTEXITCODE){throw "Native tests failed: $testName ($LASTEXITCODE)"}
}

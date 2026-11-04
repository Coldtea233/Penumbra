param([string]$Toolchain=$env:LLVM_MINGW_ROOT,[switch]$WindowEvents,[switch]$VisualProbe,[switch]$Blur,[switch]$Saturation,[switch]$Branding,[string]$BrandingExecutable,[switch]$InstallerOnly)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw directory.'}
$Toolchain=(Resolve-Path -LiteralPath $Toolchain).Path
$testDirectory=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $testDirectory|Out-Null
$testNames=@('DesktopLayeringTests','RendererTests','VignettePixelsTests')
if($InstallerOnly){$testNames=@('InstallerTests')}
if($WindowEvents){$testNames+=@('WindowPresenceTests','WindowQueryTests')}
if($VisualProbe){$testNames+='LayeredOpacityTests'}
if($Blur){$testNames+=@('RenderBudgetTests','BatchRenderBudgetTests','RenderBudgetEventsTests','EffectSamplingTests','TaskbarBoundsTests','BlurStateTests','BlurIconProtectionTests','BlurGeometryTests','BlurProbe')}
if($Saturation){$testNames+=@('SaturationStateTests','SaturationProbe')}
if($Branding){
    if(!$BrandingExecutable){throw 'Pass -BrandingExecutable with the built Nocturne.Renderer.exe.'}
    $BrandingExecutable=(Resolve-Path -LiteralPath $BrandingExecutable).Path
    $testNames+='BrandingTests'
}
foreach($testName in $testNames){
    $testExecutable=Join-Path $testDirectory "$testName.exe"
    $entryOptions=if($testName -eq 'BrandingTests'){@('-municode')}else{@()}
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot "$testName.cpp") -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static @entryOptions -luser32 -lgdi32 -lshell32 -lole32 -luuid -lpsapi -ldwmapi -lruntimeobject -lcoremessaging -ladvapi32 -lmsi -o $testExecutable
    if($LASTEXITCODE){throw "Native test compilation failed: $testName"}
    if($testName -eq 'BrandingTests'){& $testExecutable $BrandingExecutable}else{& $testExecutable}
    if($LASTEXITCODE -eq 77){Write-Warning "Visual fixture unavailable: $testName was NOT verified.";continue}
    if($LASTEXITCODE){throw "Native tests failed: $testName ($LASTEXITCODE)"}
}
if($InstallerOnly){
    $resource=Join-Path $testDirectory 'Completion.res'
    Push-Location (Join-Path $PSScriptRoot 'InstallerFixture')
    try {
        & (Join-Path (Resolve-Path -LiteralPath $Toolchain).Path 'bin\x86_64-w64-mingw32-windres.exe') Completion.rc --codepage=65001 -O coff -o $resource
        if($LASTEXITCODE){throw 'Completion dialog resource compilation failed.'}
    } finally {Pop-Location}
    $completion=Join-Path $testDirectory 'InstallerCompletionTests.exe'
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot 'InstallerCompletionTests.cpp') $resource -I (Join-Path $PSScriptRoot 'InstallerFixture') -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -luser32 -lshell32 -lole32 -luuid -lcomctl32 -lmsi -lbcrypt -ladvapi32 -o $completion
    if($LASTEXITCODE){throw 'Completion tests compilation failed.'}
    & $completion
    if($LASTEXITCODE){throw 'Completion tests failed.'}
}

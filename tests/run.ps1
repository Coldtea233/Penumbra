param([string]$Toolchain=$env:LLVM_MINGW_ROOT,[switch]$WindowEvents,[switch]$VisualProbe)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw directory.'}
$testDirectory=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $testDirectory|Out-Null
$testNames=@('DesktopLayeringTests','RendererTests')
if($WindowEvents){$testNames+='WindowPresenceTests'}
if($VisualProbe){$testNames+='LayeredOpacityTests'}
foreach($testName in $testNames){
    $testExecutable=Join-Path $testDirectory "$testName.exe"
    & (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot "$testName.cpp") -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -luser32 -lgdi32 -lshell32 -lole32 -luuid -lpsapi -ldwmapi -o $testExecutable
    if($LASTEXITCODE){throw "Native test compilation failed: $testName"}
    & $testExecutable
    if($LASTEXITCODE){throw "Native tests failed: $testName ($LASTEXITCODE)"}
}

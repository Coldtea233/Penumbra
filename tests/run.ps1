param([string]$Toolchain=$env:LLVM_MINGW_ROOT)
$ErrorActionPreference='Stop'
if(!$Toolchain){throw 'Pass -Toolchain with the llvm-mingw directory.'}
$testDirectory=Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Force $testDirectory|Out-Null
$testExecutable=Join-Path $testDirectory 'RendererTests.exe'
& (Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang++.exe') (Join-Path $PSScriptRoot 'RendererTests.cpp') -std=c++17 -O2 -Wall -Wextra -Wno-missing-field-initializers -static -luser32 -lgdi32 -lshell32 -lole32 -luuid -lpsapi -o $testExecutable
if($LASTEXITCODE){throw 'Native test compilation failed.'}
& $testExecutable
if($LASTEXITCODE){throw "Native tests failed: $LASTEXITCODE"}

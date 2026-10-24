[CmdletBinding()]
param([Parameter(Mandatory)][string]$CompilerBin)
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
$gcc=Join-Path $CompilerBin 'gcc.exe'
$gpp=Join-Path $CompilerBin 'g++.exe'
$output=Join-Path $root 'review-build'
New-Item -ItemType Directory -Path $output -Force | Out-Null
Push-Location $output
try {
    & $gcc -O2 -I..\tools\minhook\include -c ..\tools\minhook\src\buffer.c ..\tools\minhook\src\hook.c ..\tools\minhook\src\trampoline.c ..\tools\minhook\src\hde\hde64.c
    if ($LASTEXITCODE) {throw 'MinHook source compilation failed.'}
} finally {Pop-Location}
Push-Location $root
try {
    $suites=@()
    foreach ($suite in @('control_state_test','input_policy_test','control_state_fuzz','customizer_test','press_router_test','key_argument_test','editor_workflow_test')) {
        $target=Join-Path $output ($suite+'.exe')
        & $gpp -std=c++17 -O2 -Wall -Wextra -Werror -static ('customizer\tests\'+$suite+'.cpp') -o $target
        if ($LASTEXITCODE) {throw "Compilation failed: $suite"}
        $result=& $target
        if ($LASTEXITCODE) {throw "Check failed: $suite"}
        $suites+=@{suite=$suite;result='passed';output=($result -join "`n")}
        Write-Output $result
    }
    $asi=Join-Path $output 'SifuButtonControls.asi'
    # MinGW's default image base depends on the output path. Pin the original
    # preferred base so a different review directory reproduces the same bytes.
    & $gpp -std=c++17 -O2 -Wall -Wextra -Werror -static -shared customizer\src\plugin.cpp review-build\buffer.o review-build\hook.o review-build\trampoline.o review-build\hde64.o -lbcrypt '-Wl,--image-base,0x1f8280000' -o $asi
    if ($LASTEXITCODE) {throw 'Editor ASI compilation failed.'}
    $hash=(Get-FileHash -LiteralPath $asi -Algorithm SHA256).Hash
    $expected='3F2973D122D8BC3BE182FB71D2EC0D3C3FBB54AB33E020DFB27144874819304A'
    @{version='0.2.1';compiler=(& $gpp --version | Select-Object -First 1);suites=$suites;asiSha256=$hash;uploadedAsiSha256=$expected;byteIdenticalToUploaded=($hash -eq $expected)} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'review-build-report.json') -Encoding UTF8
    Write-Output ("Built ASI: {0}; identical to uploaded: {1}" -f $hash,($hash -eq $expected))
} finally {Pop-Location}

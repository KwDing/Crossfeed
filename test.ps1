param(
    [string]$WwiseRoot,
    [string]$SdkIncludeDirectory,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scripts/toolchain.ps1')
$include = Resolve-CrossfeedInclude $WwiseRoot $SdkIncludeDirectory
$header = Join-Path $include 'AK/AkWwiseSDKVersion.h'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$testInfoPath = Join-Path $build 'test-info.json'
if (Test-Path -LiteralPath $testInfoPath) { Remove-Item -LiteralPath $testInfoPath }
$info = Get-Content -LiteralPath (Join-Path $build 'build-info.json') -Raw | ConvertFrom-Json
if ($info.sdk_header_sha256 -ne (Get-FileHash -LiteralPath $header).Hash) { throw 'Test SDK differs from build SDK. Rebuild first.' }
if ($info.runtime_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $build 'Runtime/Crossfeed.dll')).Hash) { throw 'Runtime DLL differs from build metadata.' }
if ($info.authoring_sha256 -and $info.authoring_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $build 'Authoring/Crossfeed.dll')).Hash) { throw 'Authoring DLL differs from build metadata.' }
$usesPre2021Authoring = $info.authoring_api -ne 'not-built' -and !(Test-Path (Join-Path $include 'AK/Wwise/Plugin.h'))
Enable-CrossfeedCompiler -RequireAtl:$usesPre2021Authoring
Push-Location $build
try {
    foreach ($target in @('test_native','render_demo')) {
        $sources = @((Join-Path $PSScriptRoot "QA/$target.cpp"))
        if ($target -eq 'test_native') {
            $sources += Join-Path $PSScriptRoot 'QA/runtime/Signal.cpp'
            $sources += Join-Path $PSScriptRoot 'QA/runtime/Tail.cpp'
            $sources += Join-Path $PSScriptRoot 'QA/runtime/Protection.cpp'
        }
        & cl.exe '/nologo' '/std:c++17' '/EHsc' '/O2' '/MD' '/DNOMINMAX' ('/I' + $include) @sources "/Fe:$target.exe"
        if ($LASTEXITCODE) { throw "$target compile failed" }
    }
    & ./test_native.exe (Join-Path $build 'Runtime/Crossfeed.dll') (Join-Path $build 'crossfeed_impulse.csv') | Tee-Object -FilePath 'native_test.json'
    if ($LASTEXITCODE) { throw 'Runtime ABI tests failed' }
    if ($info.authoring_sha256) {
        & ./test_native.exe (Join-Path $build 'Authoring/Crossfeed.dll') | Tee-Object -FilePath 'authoring_dsp_test.json'
        if ($LASTEXITCODE) { throw 'Authoring DLL DSP/ABI tests failed' }
    }
    & ./render_demo.exe (Join-Path $build 'Runtime/Crossfeed.dll') $build
    if ($LASTEXITCODE) { throw 'Demo rendering failed' }
    & $Python (Join-Path $PSScriptRoot 'QA/analyze.py') --directory $build
    if ($LASTEXITCODE) { throw 'Signal analysis failed' }
    # The 2019/pre-2021.1 bridge has a different export and serialization API.
    # Test it with the same old SDK/Unicode ABI; this unit test is not a host test.
    if ($usesPre2021Authoring) {
        & cl.exe '/nologo' '/std:c++17' '/EHsc' '/O2' '/MD' '/DNOMINMAX' '/DUNICODE' '/D_UNICODE' ('/I' + $include) (Join-Path $PSScriptRoot 'QA/test_authoring_pre2021_1.cpp') '/Fe:test_authoring_pre2021_1.exe' '/link' 'oleaut32.lib' 'uuid.lib'
        if ($LASTEXITCODE) { throw 'Pre-2021.1 Authoring unit test compile failed' }
        & ./test_authoring_pre2021_1.exe (Join-Path $build 'Authoring/Crossfeed.dll') | Tee-Object -FilePath 'authoring_pre2021_1_test.json'
        if ($LASTEXITCODE) { throw 'Pre-2021.1 Authoring export/serialization test failed' }
    }
    [ordered]@{ status='passed'; runtime_sha256=$info.runtime_sha256; authoring_sha256=$info.authoring_sha256; sdk_header_sha256=$info.sdk_header_sha256; sdk_version=(Get-CrossfeedSdkVersion $include); authoring_host='not_run' } | ConvertTo-Json | Set-Content -LiteralPath $testInfoPath -Encoding utf8
} finally { Pop-Location }

Write-Output "Crossfeed tests PASSED (SDK $($info.sdk_version_string)). Record: $testInfoPath"

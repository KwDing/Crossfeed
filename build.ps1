param(
    [string]$WwiseRoot,
    [string]$SdkIncludeDirectory,
    [switch]$RuntimeOnly,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scripts/toolchain.ps1')
$include = Resolve-CrossfeedInclude $WwiseRoot $SdkIncludeDirectory
$version = Get-CrossfeedSdkVersion $include
$header = Join-Path $include 'AK/AkWwiseSDKVersion.h'
# Wwise introduced the new Authoring API in 2021.1 (SDK migration guide).
# Tested 2019.2.15 needs AudioPlugin.h; 2023.1.17/2025.1.8 use Plugin.h.
# Detect the available API instead of hard-coding the three tested version strings;
# this selection alone does not certify an untested SDK as compatible.
$usesPre2021Authoring = !$RuntimeOnly -and !(Test-Path -LiteralPath (Join-Path $include 'AK/Wwise/Plugin.h')) -and
    (Test-Path -LiteralPath (Join-Path $include 'AK/Wwise/AudioPlugin.h'))
if (!$RuntimeOnly -and !$usesPre2021Authoring -and !(Test-Path -LiteralPath (Join-Path $include 'AK/Wwise/Plugin.h'))) {
    throw 'No supported Authoring API in these headers. Use -RuntimeOnly for integration headers, or supply a supported full SDK.'
}
Enable-CrossfeedCompiler -RequireAtl:$usesPre2021Authoring
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path -LiteralPath $OutputDirectory).Path
$metadataPath = Join-Path $output 'build-info.json'
if (Test-Path -LiteralPath $metadataPath) { Remove-Item -LiteralPath $metadataPath }
$sources = @((Join-Path $PSScriptRoot 'src/Crossfeed.cpp'), (Join-Path $PSScriptRoot 'src/Container.cpp'))
$common = @('/nologo','/std:c++17','/EHsc','/O2','/MD','/DWIN64','/DNDEBUG','/DNOMINMAX',('/I' + $include))
$kinds = @('Runtime')
if (!$RuntimeOnly) { $kinds += 'Authoring' }
foreach ($kind in $kinds) {
    $destination = Join-Path $output $kind
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Push-Location $destination
    try {
        $arguments = $common + @('/LD')
        $inputs = $sources
        $libraries = @()
        if ($kind -eq 'Runtime') { $arguments += '/DCROSSFEED_RUNTIME_ONLY' }
        elseif ($usesPre2021Authoring) {
            # The 2019 property API uses Windows VARIANT/ATL and wide
            # property names. Unicode defines and oleaut32 are for that bridge,
            # not DSP processing or modern Authoring/runtime-only builds.
            $arguments += @('/DCROSSFEED_AUTHORING_PRE_2021_1','/DUNICODE','/D_UNICODE')
            $inputs += Join-Path $PSScriptRoot 'src/authoring/AuthoringPre2021_1.cpp'
            $libraries += 'oleaut32.lib'
        }
        & cl.exe @arguments @inputs '/Fe:Crossfeed.dll' '/link' '/INCREMENTAL:NO' @libraries
        if ($LASTEXITCODE) { throw "$kind build failed; metadata invalidated. Do not install outputs." }
    } finally { Pop-Location }
}
$authoringHash = $null
$xmlHash = $null
if (!$RuntimeOnly) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src/Crossfeed.xml') -Destination (Join-Path $output 'Authoring/Crossfeed.xml') -Force
    $authoringHash = (Get-FileHash -LiteralPath (Join-Path $output 'Authoring/Crossfeed.dll')).Hash
    $xmlHash = (Get-FileHash -LiteralPath (Join-Path $output 'Authoring/Crossfeed.xml')).Hash
}
$metadata = [ordered]@{
    sdk_header_sha256 = (Get-FileHash -LiteralPath $header).Hash
    sdk_version_string = $version
    sdk_include_directory = $include
    sdk_version = @(Get-Content -LiteralPath $header | Where-Object { $_ -match '^#define AK_WWISESDK_VERSION_(MAJOR|MINOR|SUBMINOR|BUILD)\s' })
    platform = 'Windows x64 / MSVC / optimized MD'
    authoring_api = $(if ($RuntimeOnly) { 'not-built' } elseif ($usesPre2021Authoring) { 'pre2021.1' } else { 'modern' })
    company_id = 64; plugin_id = 4106
    authoring_sha256 = $authoringHash
    authoring_xml_sha256 = $xmlHash
    runtime_sha256 = (Get-FileHash -LiteralPath (Join-Path $output 'Runtime/Crossfeed.dll')).Hash
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath $metadataPath -Encoding utf8
Write-Output "Built Crossfeed SDK $version ($($metadata.authoring_api)) in $output. No files installed. Run test.ps1."

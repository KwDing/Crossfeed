param(
    [string]$WwiseRoot,
    [string]$SdkIncludeDirectory,
    [string]$UnrealIntegrationRoot,
    [string]$RuntimePlatform = 'x64_vc170',
    [string[]]$Configurations = @('Profile','Release'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot 'build'),
    [switch]$Authoring,
    [string[]]$RuntimeBinDirectories = @(),
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
function Assert-CrossfeedInstallHostsClosed([switch]$IncludeRuntime) {
    $hostsToClose = @('Wwise','WwiseConsole')
    # Authoring writes only to the Wwise application. Engine process checks
    # apply when the install plan also contains a runtime destination.
    if ($IncludeRuntime) { $hostsToClose += 'UnrealEditor' }
    if (Get-Process -Name $hostsToClose -ErrorAction SilentlyContinue) {
        throw ('Save and close ' + ($hostsToClose -join '/') + ' before installing.')
    }
}
. (Join-Path $PSScriptRoot 'scripts/toolchain.ps1')
if ($UnrealIntegrationRoot) {
    if ($Authoring -or $WwiseRoot -or $SdkIncludeDirectory -or $RuntimeBinDirectories.Count) {
        throw '-UnrealIntegrationRoot is a separate runtime install flow. Do not combine with Authoring, SDK or explicit bin arguments.'
    }
    $integration = (Resolve-Path -LiteralPath $UnrealIntegrationRoot).Path
    if (!(Get-ChildItem -LiteralPath $integration -Filter '*.uplugin' -File)) { throw 'Integration root must contain its .uplugin descriptor.' }
    if ($RuntimePlatform -notmatch '^x64_vc\d+$') { throw 'Only explicit Windows x64 vc toolchain directories are supported.' }
    $SdkIncludeDirectory = Join-Path $integration 'ThirdParty/include'
    foreach ($configuration in $Configurations) {
        if ($configuration -notin @('Profile','Release')) { throw 'This build supports Profile/Release installation only; Debug/static CRT need their own build.' }
        $RuntimeBinDirectories += Join-Path $integration "ThirdParty/$RuntimePlatform/$configuration/bin"
    }
}
if ($Authoring -and !$WwiseRoot) { throw '-Authoring requires the actual installed -WwiseRoot.' }
if (!$Authoring -and !$RuntimeBinDirectories.Count) { throw 'Select -Authoring, -UnrealIntegrationRoot or explicit -RuntimeBinDirectories.' }
$include = Resolve-CrossfeedInclude $WwiseRoot $SdkIncludeDirectory
$header = Join-Path $include 'AK/AkWwiseSDKVersion.h'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$info = Get-Content -LiteralPath (Join-Path $build 'build-info.json') -Raw | ConvertFrom-Json
if ($Authoring -and (!$info.authoring_sha256 -or $info.authoring_api -eq 'not-built')) { throw 'This is a runtime-only build. Build Authoring against the installed full SDK first.' }
if ($info.sdk_header_sha256 -ne (Get-FileHash -LiteralPath $header).Hash) { throw 'Target SDK differs from build SDK; rebuild against these target headers and test first.' }
$testInfoPath = Join-Path $build 'test-info.json'
if (!(Test-Path -LiteralPath $testInfoPath)) { throw 'Run test.ps1 successfully for this build before installing.' }
$testInfo = Get-Content -LiteralPath $testInfoPath -Raw | ConvertFrom-Json
if ($testInfo.status -ne 'passed' -or $testInfo.runtime_sha256 -ne $info.runtime_sha256 -or $testInfo.sdk_header_sha256 -ne $info.sdk_header_sha256) { throw 'Tests do not match this runtime DLL/SDK. Rerun test.ps1.' }
$plan = @()
if ($Authoring) {
    if ($testInfo.authoring_sha256 -ne $info.authoring_sha256) { throw 'Tests do not match this Authoring DLL. Rerun test.ps1.' }
    $pluginDirectory = Join-Path $WwiseRoot 'Authoring/x64/Release/bin/Plugins'
    if (!(Test-Path -LiteralPath $pluginDirectory -PathType Container)) { throw "Unsupported authoring layout: $pluginDirectory" }
    foreach ($candidate in (Get-ChildItem -LiteralPath $pluginDirectory -Filter '*.xml')) {
        if ($candidate.Name -eq 'Crossfeed.xml') { continue }
        [xml]$module = Get-Content -LiteralPath $candidate.FullName -Raw
        $duplicate = $module.SelectNodes('//EffectPlugin') | Where-Object { $_.CompanyID -eq '64' -and $_.PluginID -eq '4106' }
        if ($duplicate) { throw "Duplicate 64/4106 in $($candidate.FullName). Resolve the duplicate plugin identity before installation." }
    }
    foreach ($extension in @('dll','xml')) {
        $plan += [pscustomobject]@{ Source = (Join-Path $build "Authoring/Crossfeed.$extension"); Destination = (Join-Path $pluginDirectory "Crossfeed.$extension") }
    }
}
foreach ($directory in $RuntimeBinDirectories) {
    if (!(Test-Path -LiteralPath $directory -PathType Container)) { throw "Runtime bin directory must already exist: $directory" }
    $plan += [pscustomobject]@{ Source = (Join-Path $build 'Runtime/Crossfeed.dll'); Destination = (Join-Path (Resolve-Path -LiteralPath $directory).Path 'Crossfeed.dll') }
}
foreach ($row in $plan) {
    if (!(Test-Path -LiteralPath $row.Source -PathType Leaf)) { throw "Build first: $($row.Source)" }
}
if ($info.runtime_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $build 'Runtime/Crossfeed.dll')).Hash) { throw 'Runtime DLL changed since build metadata; rebuild/test first.' }
if ($Authoring) {
    if ($info.authoring_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $build 'Authoring/Crossfeed.dll')).Hash) { throw 'Authoring DLL changed since build metadata.' }
    if ($info.authoring_xml_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $build 'Authoring/Crossfeed.xml')).Hash -or
        $info.authoring_xml_sha256 -ne (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'src/Crossfeed.xml')).Hash) { throw 'XML changed since build; rebuild first.' }
}
$plan | Format-List Source,Destination
if (!$Apply) { Write-Output 'Preview only. Close all target hosts, then repeat with -Apply to install.'; return }
Assert-CrossfeedInstallHostsClosed -IncludeRuntime:($RuntimeBinDirectories.Count -gt 0)
$backup = Join-Path $build ('install-backup/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $backup | Out-Null
# Snapshot the whole plan before any destination write, so an interrupted copy can be restored.
$index = 0
$manifest = foreach ($row in $plan) {
    $previous = Join-Path $backup ("$index-" + (Split-Path $row.Destination -Leaf))
    $existed = Test-Path -LiteralPath $row.Destination
    if ($existed) { Copy-Item -LiteralPath $row.Destination -Destination $previous }
    [pscustomobject]@{ Destination = $row.Destination; Existed = $existed; Backup = $previous }
    $index++
}
@($manifest) | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'restore-manifest.json') -Encoding utf8
foreach ($row in $plan) {
    Copy-Item -LiteralPath $row.Source -Destination $row.Destination -Force
    if ((Get-FileHash -LiteralPath $row.Source).Hash -ne (Get-FileHash -LiteralPath $row.Destination).Hash) { throw "Install hash mismatch; restore using $backup" }
}
Write-Output "Installed. Restore manifest: $backup/restore-manifest.json. Regenerate SoundBanks and verify the host."

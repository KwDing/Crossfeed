# Shared by build/test. Resolve vcvars, then invoke cl with an argument array.
function Enable-CrossfeedCompiler([switch]$RequireAtl) {
    # Reuse an initialized x64 environment. Repeated vcvars calls otherwise grow
    # PATH until cmd.exe exceeds its environment expansion limit in matrix runs.
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VCToolsInstallDir -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') {
        if (!$RequireAtl -or (Test-Path -LiteralPath (Join-Path $env:VCToolsInstallDir 'atlmfc/include/atlstr.h'))) { return }
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ tools (vswhere missing).' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vs) { throw 'MSVC x64 C++ toolset not found.' }
    $setup = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    $toolset = ''
    if ($RequireAtl) {
        $available = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'atlmfc/include/atlstr.h') } |
            Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        if (!$available) { throw 'Wwise 2019.2 Authoring requires the Visual Studio C++ ATL component. Runtime-only does not.' }
        $toolset = ' -vcvars_ver=' + $available.Name
    }
    $variables = & $env:ComSpec /d /c "call `"$setup`"$toolset >nul && set"
    if ($LASTEXITCODE) { throw 'vcvars64 failed.' }
    foreach ($line in $variables) {
        if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
    }
    if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe missing after vcvars64.' }
}

function Get-CrossfeedSdkHeader([string]$WwiseRoot) {
    $header = Join-Path $WwiseRoot 'SDK/include/AK/AkWwiseSDKVersion.h'
    if (!(Test-Path -LiteralPath $header)) { throw "Wwise SDK not installed: $header" }
    return (Resolve-Path -LiteralPath $header).Path
}

function Resolve-CrossfeedInclude([string]$WwiseRoot,[string]$SdkIncludeDirectory) {
    if ($WwiseRoot -and $SdkIncludeDirectory) { throw 'Specify either -WwiseRoot or -SdkIncludeDirectory, not both.' }
    if ($WwiseRoot) { $SdkIncludeDirectory = Join-Path $WwiseRoot 'SDK/include' }
    if (!$SdkIncludeDirectory) { throw 'Specify -WwiseRoot or -SdkIncludeDirectory.' }
    $header = Join-Path $SdkIncludeDirectory 'AK/AkWwiseSDKVersion.h'
    if (!(Test-Path -LiteralPath $header)) { throw "SDK version header missing: $header" }
    return (Resolve-Path -LiteralPath $SdkIncludeDirectory).Path
}

function Get-CrossfeedSdkVersion([string]$IncludeDirectory) {
    $source = Get-Content -LiteralPath (Join-Path $IncludeDirectory 'AK/AkWwiseSDKVersion.h') -Raw
    $values = foreach ($part in @('MAJOR','MINOR','SUBMINOR','BUILD')) {
        $match = [regex]::Match($source, "(?m)^#define\s+AK_WWISESDK_VERSION_$part\s+(\d+)")
        if (!$match.Success) { throw "SDK version macro missing: $part" }
        $match.Groups[1].Value
    }
    return $values -join '.'
}

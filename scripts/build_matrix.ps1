param(
    [Parameter(Mandatory=$true)][string[]]$WwiseRoots,
    [string]$OutputRoot = (Join-Path $PSScriptRoot '../build/matrix'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'toolchain.ps1')
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$output = (Resolve-Path -LiteralPath $OutputRoot).Path
$results = @()
foreach ($root in $WwiseRoots) {
    $row = [ordered]@{ sdk_root=$root; version=$null; compile='not_run'; runtime_tests='not_run'; authoring_host='not_run'; ue_game='not_run'; error=$null }
    try {
        $include = Resolve-CrossfeedInclude $root ''
        $row.version = Get-CrossfeedSdkVersion $include
        $destination = Join-Path $output $row.version
        & (Join-Path $PSScriptRoot '../build.ps1') -WwiseRoot $root -OutputDirectory $destination
        $row.compile = 'passed'
        & (Join-Path $PSScriptRoot '../test.ps1') -WwiseRoot $root -BuildDirectory $destination -Python $Python
        $row.runtime_tests = 'passed'
    } catch {
        if ($row.compile -eq 'not_run') { $row.compile = 'failed' } else { $row.runtime_tests = 'failed' }
        $row.error = $_.Exception.Message
    }
    $results += [pscustomobject]$row
    ConvertTo-Json -InputObject @($results) -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'compatibility-results.json') -Encoding utf8
}
$results | Format-Table version,compile,runtime_tests,authoring_host,ue_game -AutoSize
if ($results | Where-Object { $_.compile -ne 'passed' -or $_.runtime_tests -ne 'passed' }) { throw 'Some SDK checks failed. Inspect compatibility-results.json; no host/game verification was performed.' }

# Copyright (c) 2026 Алексей Савельев. LicenseRef-OUT-Source-Review-1.0.
param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$FiberRoot = Split-Path -Parent $PSScriptRoot
$ReportDirectory = Join-Path $FiberRoot 'benchmarks'
$cases = @(
    @{n=512;w=1;t=1200;mode='exploratory';full=$false},
    @{n=4096;w=1;t=1200;mode='exploratory';full=$false},
    @{n=4096;w=8;t=1200;mode='exploratory';full=$false},
    @{n=4096;w=8;t=1200;mode='strict';full=$false},
    @{n=4096;w=8;t=1200;mode='overlap';full=$false},
    @{n=4096;w=8;t=1200;mode='utility';full=$false},
    @{n=16384;w=8;t=600;mode='exploratory';full=$false},
    @{n=65536;w=8;t=300;mode='exploratory';full=$false},
    @{n=4096;w=8;t=600;mode='exploratory';full=$true},
    @{n=16384;w=8;t=300;mode='exploratory';full=$true},
    @{n=65536;w=8;t=180;mode='exploratory';full=$true}
)
foreach ($case in $cases) {
    $suffix = if ($case.full) { '-full-ai' } else { '' }
    $file = Join-Path $ReportDirectory "$($case.n)-$($case.mode)-$($case.w)w$suffix.json"
    $options = @('--bench','--agents',$case.n,'--workers',$case.w,'--ticks',$case.t,'--warmup',120,'--seed',41917,'--mode',$case.mode,'--output',$file)
    if ($case.full) { $options += '--full-ai' }
    & $Executable @options | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $file" }
    $r = Get-Content -LiteralPath $file -Raw | ConvertFrom-Json
    Write-Output "$($case.n) / $($case.mode) / $($case.w)w / full=$($case.full): p95=$($r.tickMs.p95)ms; alive=$($r.aliveAtEnd); capped=$($r.cappedPerceptions); allocations=$($r.trackedHeapAllocationsDuringSteps)"
}
$one = Get-Content (Join-Path $ReportDirectory '4096-exploratory-1w.json') -Raw | ConvertFrom-Json
$eight = Get-Content (Join-Path $ReportDirectory '4096-exploratory-8w.json') -Raw | ConvertFrom-Json
if ($one.replayDigest -ne $eight.replayDigest) { throw 'Replay differs at 1 and 8 workers' }
Write-Output 'PASS: final 4096-actor benchmark replay identical at 1 and 8 workers.'

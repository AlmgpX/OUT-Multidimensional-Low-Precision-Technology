# SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
# Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$FiberRoot = Split-Path -Parent $PSScriptRoot
$ReportDirectory = Join-Path $FiberRoot 'benchmarks/v02'
New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$cases = @(
    @{n=256;w=1;t=1200;mode='exploratory';full=$false;lod=$true},
    @{n=4096;w=1;t=1200;mode='exploratory';full=$false;lod=$true},
    @{n=4096;w=8;t=1200;mode='exploratory';full=$false;lod=$true},
    @{n=4096;w=8;t=1200;mode='exploratory';full=$false;lod=$false},
    @{n=4096;w=8;t=1200;mode='strict';full=$false;lod=$true},
    @{n=4096;w=8;t=1200;mode='utility';full=$false;lod=$true},
    @{n=16384;w=8;t=600;mode='exploratory';full=$false;lod=$true},
    @{n=65536;w=8;t=300;mode='exploratory';full=$false;lod=$true},
    @{n=4096;w=8;t=600;mode='exploratory';full=$true;lod=$true},
    @{n=16384;w=8;t=300;mode='exploratory';full=$true;lod=$true},
    @{n=65536;w=8;t=180;mode='exploratory';full=$true;lod=$true}
)
foreach ($case in $cases) {
    $suffix = if ($case.full) { '-full-ai' } elseif (-not $case.lod) { '-no-lod' } else { '-lod' }
    $file = Join-Path $ReportDirectory "$($case.n)-$($case.mode)-$($case.w)w$suffix.json"
    $options = @('--bench','--agents',$case.n,'--workers',$case.w,'--ticks',$case.t,'--warmup',120,'--seed',41917,'--mode',$case.mode,'--output',$file)
    if ($case.full) { $options += '--full-ai' }
    if (-not $case.lod) { $options += '--no-lod' }
    & $Executable @options | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $file" }
    $r = Get-Content -LiteralPath $file -Raw -Encoding UTF8 | ConvertFrom-Json
    Write-Output "$($case.n) / $($case.w)w / $suffix : p95=$($r.tickMs.p95)ms; decisions=$($r.actualDecisions); allocations=$($r.trackedHeapAllocationsDuringSteps)"
}
$one = Get-Content (Join-Path $ReportDirectory '4096-exploratory-1w-lod.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$eight = Get-Content (Join-Path $ReportDirectory '4096-exploratory-8w-lod.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($one.replayDigest -ne $eight.replayDigest) { throw 'Replay differs at 1 and 8 workers' }
Write-Output 'PASS: Tales 0.2 replay identical at 1 and 8 workers.'

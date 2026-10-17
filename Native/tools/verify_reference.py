# SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
# Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
"""Publisher differential checks. Raw synthetic observations stay transient, outside Git."""
from pathlib import Path
import argparse, collections, datetime, json, math, subprocess, tempfile

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--executable',required=True)
parser.add_argument('--output',default=str(root/'Native/results/PARITY.json'))
args=parser.parse_args()
executable=str(Path(args.executable).resolve())
project=root/'Native/tests/Reference.csproj'
subprocess.run(['dotnet','build',str(project),'-c','Release','--nologo'],check=True,cwd=root)
assembly=root/'Native/tests/bin/Release/net10.0/Reference.dll'
def run(command):
    return subprocess.check_output(command,cwd=root,text=True,encoding='utf-8',errors='strict')
reference=run(['dotnet',str(assembly)]).splitlines()
native=run([executable,'--reference']).splitlines()
assert len(reference)==len(native),(len(reference),len(native))
rows=collections.Counter();max_error=collections.defaultdict(float);values=0
for number,(a,b) in enumerate(zip(reference,native),1):
    av,bv=a.split('\t'),b.split('\t')
    assert av[0]==bv[0] and len(av)==len(bv),f'Row schema mismatch at {number}'
    tag=av[0];rows[tag]+=1
    for column,(x,y) in enumerate(zip(av[1:],bv[1:]),1):
        values+=1
        if tag in {'K','H','F'}:
            assert x==y,f'Binary format mismatch {tag}:{column}'
            continue
        xf,yf=float(x),float(y)
        assert math.isfinite(xf) and math.isfinite(yf),f'Nonfinite result {tag}:{column}'
        error=abs(xf-yf);max_error[tag]=max(max_error[tag],error)
        tolerance=2e-5 if tag=='P' else 2e-4 if tag=='G' else 1e-9
        assert math.isclose(xf,yf,abs_tol=tolerance,rel_tol=tolerance),f'Parity mismatch row={number} tag={tag} col={column}: C#={x} C++={y}'
with tempfile.TemporaryDirectory(prefix='OUT-native-interop-') as directory:
    run(['dotnet',str(assembly),'--interop',directory])
    run([executable,'--interop',directory])
    run(['dotnet',str(assembly),'--interop-verify',directory])
result={
    'format':'OUT-NATIVE-REFERENCE-PARITY-1',
    'localDate':str(datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=3))).date()),
    'reference':'Original repository C# Guard core, Motor MotionCache and FootPlanner sources',
    'rows':len(reference),'comparedValues':values,'rowsByKind':dict(rows),
    'maximumAbsoluteDifference':dict(max_error),
    'doubleTolerance':1e-9,'motionFloatTolerance':2e-5,'footFloatTolerance':2e-4,
    'interop':['C++ decodes original C# AES-256-CBC/HMAC envelope','C# decodes native envelope','Native verifies original C# RSA-2048/SHA256 manifest and rejects edited signature'],
    'passed':True,
    'scope':'Windows MSVC/.NET 10; synthetic fixture only. Motor reference uses test-only Vector3/Quaternion/Mathf shim, not Unity physics/rendering or withheld licensed banks. This is not full-game or cross-platform certification.'
}
out=Path(args.output);out.parent.mkdir(parents=True,exist_ok=True)
out.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
print(f'PASS {len(reference)} reference rows / {values} values, bidirectional save interop and RSA manifest verification.')

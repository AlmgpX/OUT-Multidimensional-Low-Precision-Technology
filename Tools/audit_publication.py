# SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
# Copyright (c) 2026 Алексей Савельев (Alex Merqury). LicenseRef-OUT-Source-Review-1.0. See LICENSE.
"""Publisher audit: prevent accidental publication of data-bearing files and encoded banks."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
root=Path(__file__).resolve().parents[1]
provenance=json.loads((root/'PROVENANCE.json').read_text(encoding='utf-8'))
errors=[]
fiber_snapshot=root/'OUT_FIBER/SNAPSHOT.json'
if fiber_snapshot.is_file():
 snapshot=json.loads(fiber_snapshot.read_text(encoding='utf-8'))
 for name,expected in snapshot['sourceSha256'].items():
  p=root/'OUT_FIBER'/name
  if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
   errors.append('OUT FIBER source hash mismatch: '+name)
native_snapshot=root/'Native/SNAPSHOT.json'
if native_snapshot.is_file():
 native=json.loads(native_snapshot.read_text(encoding='utf-8'))
 for name,expected in native['sourceSha256'].items():
  p=root/name
  if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
   errors.append('Native Guard/Motor source hash mismatch: '+name)
for item in provenance['files']:
 p=root/item['path']
 if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=item['publishedSha256']:
  errors.append('Source snapshot mismatch: '+item['path'])
try:
 listed=subprocess.check_output(['git','ls-files','--cached'],cwd=root,text=True).splitlines()
except subprocess.CalledProcessError:
 listed=[str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and '.git' not in p.parts and 'bin' not in p.parts and 'obj' not in p.parts]
if not listed:
 listed=[str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and '.git' not in p.parts and 'bin' not in p.parts and 'obj' not in p.parts]
blocked_names={'GuardBuiltInTemporalModel.cs','OUTMotorData.cs','BakedModel_manifest.json'}
blocked_ext={'.asset','.fbx','.glb','.gltf','.dll','.exe','.lib','.obj','.pdb','.exp','.ilk','.bin','.save','.ogsave','.hex','.ofib','.onnx','.npy','.npz','.csv','.jsonl','.zip','.tgz','.unitypackage'}
checked=0;script_count=0
for name in listed:
 p=root/name
 if p.name in blocked_names or p.suffix.lower() in blocked_ext:
  errors.append('Data/binary-bearing file: '+name);continue
 data=p.read_bytes();checked+=1
 if p.suffix.lower()=='.png':
  snapshot=json.loads((root/'OUT_FIBER/SNAPSHOT.json').read_text(encoding='utf-8'))
  expected=snapshot.get('nativeScreenshots',{}).get(name.replace('\\','/'))
  if name.replace('\\','/')=='OUT_FIBER/docs/OUT_FIBER_Heather_Reach.png':expected=expected or snapshot.get('nativeScreenshotSha256')
  if not expected or not data.startswith(b'\x89PNG\r\n\x1a\n') or hashlib.sha256(data).hexdigest()!=expected:
   errors.append('Native screenshot signature/hash mismatch: '+name)
  continue
 try:text=data.decode('utf-8-sig')
 except UnicodeDecodeError:errors.append('Unexpected binary: '+name);continue
 if p.suffix in {'.cs','.cpp','.h','.txt'} and re.search(r'"[A-Za-z0-9+/]{200,}={0,2}"',text):
  errors.append('Long encoded payload: '+name)
 if re.search(r'gh[pousr]_[A-Za-z0-9]{30,}|-----BEGIN (?:RSA |OPENSSH )?PRIVATE KEY-----',text):
  errors.append('Potential secret: '+name)
 script=p.suffix.lower() in {'.cs','.c','.cc','.cxx','.cpp','.h','.hpp','.py','.js','.cjs','.mjs','.ps1','.cmd','.bat','.sh','.csproj','.props'} or p.name=='CMakeLists.txt' or name.endswith('.cs.txt')
 if script:
  script_count+=1
  if not text.splitlines() or 'SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0' not in text.splitlines()[0]:
   errors.append('Script must begin with the license identifier: '+name)
# The initial publication must not inherit the product repository's old data/history.
try:
 objects=subprocess.check_output(['git','rev-list','--objects','--all'],cwd=root,text=True,stderr=subprocess.DEVNULL)
 for line in objects.splitlines():
  parts=line.split(' ',1)
  if len(parts)==2:
   p=Path(parts[1])
   if p.name in blocked_names or p.suffix.lower() in blocked_ext:
    errors.append('Forbidden history path: '+parts[1])
except subprocess.CalledProcessError:pass
if errors:
 print('\n'.join(errors));sys.exit(1)
print(f'PASS: {checked} publication files; {len(provenance["files"])} source hashes match; no blocked dataset/binary paths or long encoded banks found.')
print(f'PASS: {script_count} scripts/source/build files begin with the license identifier.')
print('Scope: source allowlist and payload heuristics; not a proof of legal ownership or a general secret scanner.')

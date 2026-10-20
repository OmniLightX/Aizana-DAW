#!/usr/bin/env python3
"""Read-only release bundle checks. No signing or security-setting changes."""
import argparse, plistlib, re, subprocess
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('app',type=Path)
a=parser.parse_args(); app=a.app.resolve(); failures=[]; binaries=[]
info=plistlib.loads((app/'Contents/Info.plist').read_bytes())
for p in app.rglob('*'):
 if not p.is_file():continue
 data=p.read_bytes()
 # ONNX upstream CI embeds its public runner source root, not a maintainer home.
 checked=data
 if p.name == 'libonnxruntime.1.20.1.dylib':
  checked=checked.replace(b'/Users/runner/work/1/s/', b'upstream-onnx/').replace(b'/Users/runner/work/1/b/', b'upstream-onnx-build/')
 # Personal home and removable-volume paths must not survive into the package.
 if re.search(rb'/Users/(?!Shared(?:/|\x00))[^/\x00\s]+/',checked) or re.search(rb'/Volumes/[^\x00\s]+', data):
  failures.append(f'personal path in {p.relative_to(app)}')
 if b'-----BEGIN PRIVATE KEY-----' in data or b'-----BEGIN OPENSSH PRIVATE KEY-----' in data:
  failures.append(f'private key in {p.relative_to(app)}')
 if data[:4] in (b'\xcf\xfa\xed\xfe',b'\xca\xfe\xba\xbe',b'\xfe\xed\xfa\xcf'):
  binaries.append(str(p.relative_to(app)))
  load_commands=subprocess.check_output(['otool','-l',str(p)],text=True)
  minimums=re.findall(r'\bminos\s+(\d+(?:\.\d+)*)',load_commands)
  for minimum in minimums:
   parts=tuple(int(x) for x in minimum.split('.'))
   if (parts+(0,0,0))[:3] > (13,3,0):
    failures.append(f'macOS deployment target {minimum} exceeds 13.3 in {p.relative_to(app)}')
  deps=subprocess.check_output(['otool','-L',str(p)],text=True)
  for line in deps.splitlines():
   if not line.startswith('\t'):continue
   name=line.strip().split(' (compatibility')[0]
   if name.startswith(('/opt/','/usr/local/','/Users/')):
    failures.append(f'external library dependency in {p.relative_to(app)}: {name}')
for name in ('COPYING','COPYING.AGPL','LICENSE.md','THIRD_PARTY_NOTICES.md','DISTRIBUTION.md'):
 if not (app/'Contents/Resources/Licenses'/name).exists():failures.append('missing license '+name)
print('Version:',info.get('CFBundleShortVersionString'))
print('Mach-O files:',len(binaries))
for b in binaries:print(' ',b)
for f in failures:print('FAIL:',f)
if failures:raise SystemExit(1)
print('PASS: selected privacy, dependency and license checks; notarization/live testing separate')

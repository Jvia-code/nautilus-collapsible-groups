#!/usr/bin/env python3
"""Build and verify a self-contained checkpoint. Remote saving is a separate step."""
from pathlib import Path
import hashlib, zipfile, sys
root=Path(__file__).resolve().parents[1]
output=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root.parent/'nautilus-groupes-reprise.zip'
files=sorted(p for p in root.rglob('*') if p.is_file() and p.name!='SHA256SUMS' and '__pycache__' not in p.parts)
manifest=''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(root).as_posix()+'\n' for p in files)
(root/'SHA256SUMS').write_text(manifest)
files.append(root/'SHA256SUMS')
with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for p in files:z.write(p,'nautilus-groupes/'+p.relative_to(root).as_posix())
with zipfile.ZipFile(output) as z:
 assert z.testzip() is None
 for p in files:
  assert z.read('nautilus-groupes/'+p.relative_to(root).as_posix())==p.read_bytes()
print(f'ZIP verified: {len(files)} files, {output.stat().st_size} bytes')
print(f'SHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')
print(output)

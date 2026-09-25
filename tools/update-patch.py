#!/usr/bin/env python3
from pathlib import Path
import tarfile,difflib
r=Path(__file__).resolve().parents[1];patch=[];changed=[]
with tarfile.open(r/'vendor/nautilus-50.3.tar.xz') as t:
 for m in t.getmembers():
  if not m.isfile():continue
  relative=Path(m.name).relative_to('nautilus-50.3');p=r/'src-work'/m.name
  a=t.extractfile(m).read();b=p.read_bytes()
  if a!=b:
   changed.append(str(relative));patch.extend(difflib.unified_diff(a.decode().splitlines(True),b.decode().splitlines(True),fromfile='a/'+str(relative),tofile='b/'+str(relative)))
(r/'nautilus-groupes.patch').write_text(''.join(patch))
(r/'validation/modified-files.txt').write_text('\n'.join(changed)+'\n')
print('Modified source files:',len(changed))

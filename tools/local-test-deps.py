#!/usr/bin/env python3
"""Optional Ubuntu 24.04 test dependencies in a local prefix (no apt/chroot).
Not used by the Fedora installer. Does not build Nautilus 50.3 in full.
"""
import concurrent.futures,gzip,hashlib,json,os,pathlib,re,subprocess,sys,urllib.request
cache=pathlib.Path(sys.argv[1]).resolve();cache.mkdir(parents=True,exist_ok=True)
packages={}
for component in ['main','universe']:
 target=cache/(component+'-Packages.gz')
 if not target.exists():urllib.request.urlretrieve('https://archive.ubuntu.com/ubuntu/dists/noble/'+component+'/binary-amd64/Packages.gz',target)
 for stanza in gzip.open(target,'rt').read().split('\n\n'):
  d={}
  for line in stanza.splitlines():
   if line and not line[0].isspace() and ': ' in line:
    k,v=line.split(': ',1);d[k]=v
  if 'Package' in d:packages[d['Package']]=d
installed=set()
for line in subprocess.check_output(['dpkg-query','-W','-f','${binary:Package} ${db:Status-Abbrev}\n'],text=True).splitlines():
 if line.endswith('ii '):installed.add(line.split()[0].split(':')[0])
want=set();missing=set()
def add(n):
 n=n.split(':')[0]
 if n in want or n in installed:return
 if n not in packages:missing.add(n);return
 want.add(n)
 for part in (packages[n].get('Depends','')+','+packages[n].get('Pre-Depends','')).split(','):
  alts=[re.split(r'[ (]',a.strip())[0] for a in part.split('|') if a.strip()]
  if not alts or any(a in installed for a in alts):continue
  add(next((a for a in alts if a in packages),alts[0]))
for n in ['libgtk-4-dev','libadwaita-1-dev','pkgconf','xvfb','libportal-dev','libportal-gtk4-dev','libgnome-autoar-0-dev']:add(n)
print('Packages:',len(want),'Unresolved virtual names:',sorted(missing),flush=True)
(cache/'debs').mkdir(exist_ok=True)
def fetch(n):
 d=packages[n];p=cache/'debs'/(n+'.deb')
 if not p.exists():urllib.request.urlretrieve('https://archive.ubuntu.com/ubuntu/'+d['Filename'],p)
 assert hashlib.sha256(p.read_bytes()).hexdigest()==d['SHA256'],n
 subprocess.run(['dpkg-deb','-x',str(p),str(cache/'root')],check=True)
 return n
with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
 for n in pool.map(fetch,sorted(want)):print(n,flush=True)
# Development symlinks can refer to runtime packages already installed on host.
lib=cache/'root/usr/lib/x86_64-linux-gnu'
for p in lib.iterdir():
 if p.is_symlink() and not p.exists():
  host=pathlib.Path('/usr/lib/x86_64-linux-gnu')/p.readlink().name
  if host.exists():p.unlink();p.symlink_to(host)
(cache/'bin').mkdir(exist_ok=True)
p=cache/'bin/pkg-config'
if p.is_symlink():p.unlink()
p.symlink_to(cache/'root/usr/bin/pkgconf')
import shlex
q=shlex.quote
(cache/'env.sh').write_text('\n'.join([
 'export PATH='+q(str(cache/'bin'))+':"$PATH"',
 'export PKG_CONFIG_SYSROOT_DIR='+q(str(cache/'root')),
 'export PKG_CONFIG_LIBDIR='+q(':'.join([str(lib/'pkgconfig'),str(cache/'root/usr/share/pkgconfig'),'/usr/lib/x86_64-linux-gnu/pkgconfig','/usr/share/pkgconfig'])),
 'export LD_LIBRARY_PATH='+q(str(lib)+':'+str(cache/'root/lib/x86_64-linux-gnu')),'']))
print('Ready:',cache/'env.sh',flush=True)

#!/usr/bin/env python3
"""Partial syntax validation on Ubuntu GTK 4.14; not a Fedora/full build."""
from pathlib import Path
import os,shlex,subprocess,sys,json
r=Path(__file__).resolve().parents[1];cache=Path(sys.argv[1]).resolve();source=r/'src-work/nautilus-50.3';headers=cache/'headers';headers.mkdir(exist_ok=True)
(headers/'libnautilus-extension').mkdir(exist_ok=True)
mkenums=cache/'root/usr/bin/glib-mkenums';template=source/'src/nautilus-enum-types.h.template'
for out,inputs in [(headers/'nautilus-enum-types.h',[source/'src/nautilus-enums.h',source/'src/nautilus-search-popover.h']),
                   (headers/'libnautilus-extension/nautilus-extension-enum-types.h',[source/'libnautilus-extension/nautilus-info-provider.h'])]:
 text=subprocess.check_output([sys.executable,str(mkenums),'--template',str(template),*map(str,inputs)],text=True)
 if 'extension' in out.name:text=text.replace('NAUTILUS_ENUM_TYPES_H','NAUTILUS_EXTENSION_ENUM_TYPES_H')
 out.write_text(text)
(headers/'config.h').write_text('''#define APPLICATION_ID "org.gnome.Nautilus.GroupesTest"
#define GETTEXT_PACKAGE "nautilus"
#define VERSION "50.3"
#define PACKAGE_VERSION "50.3"
#define PROFILE "Devel"
#define NAUTILUS_DATADIR "/usr/share/nautilus"
#define NAUTILUS_EXTENSIONDIR "/usr/lib/nautilus/extensions-4"
#define HAVE_SELINUX 0
#define HAVE_CLOUDPROVIDERS 0
#define NAME_SUFFIX " (Groupes - test)"
''')
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','gtk4','libadwaita-1','libportal-gtk4','gnome-autoar-0'],text=True))
results=[]
for name in ['nautilus-view-model','nautilus-view-item','nautilus-list-view','nautilus-list-base','nautilus-files-view']:
 args=['cc','-std=c11','-D_GNU_SOURCE','-fsyntax-only','-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types']
 compatibility=[]
 if name=='nautilus-list-base':compatibility=['-DGDK_ACTION_NONE=0']
 args+=compatibility+['-I'+str(p) for p in [headers,source,source/'src',source/'libnautilus-extension']]+flags+[str(source/'src'/(name+'.c'))]
 run=subprocess.run(args,text=True,capture_output=True)
 (r/'validation'/(name+'-syntax.log')).write_text(run.stdout+run.stderr)
 results.append({'source':name+'.c','exit_code':run.returncode,'local_compatibility_defines':compatibility})
 print(name,run.returncode)
(r/'validation/syntax-results.json').write_text(json.dumps({'scope':'Syntax only; GTK 4.14.2, generated local config.h; no linking or complete Fedora build','results':results},indent=2)+'\n')
sys.exit(any(x['exit_code'] for x in results))

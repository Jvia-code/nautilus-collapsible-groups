#!/usr/bin/env python3
"""Build and install a private Nautilus test build; never replace Fedora Nautilus."""
from __future__ import annotations
import argparse,hashlib,json,os,pathlib,shlex,shutil,subprocess,sys,tempfile,time
PACKAGE=pathlib.Path(__file__).resolve().parent
APP_ID='org.gnome.Nautilus.GroupesTest'
MARKER='nautilus-groupes-owned-v1.json'

def locations(home):
    return {'prefix':home/'.local/opt/nautilus-groupes-50.3',
            'launcher':home/'.local/bin/nautilus-groupes',
            'desktop':home/'.local/share/applications/org.gnome.Nautilus.GroupesTest.desktop',
            'state':home/'.local/state/nautilus-groupes',
            'cache':home/'.cache/nautilus-groupes-50.3'}

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def command(args,log=None,env=None):
    print('+ '+shlex.join(map(str,args)),flush=True)
    process=subprocess.Popen(list(map(str,args)),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,env=env)
    for line in process.stdout:
        print(line,end='',flush=True)
        if log:log.write(line);log.flush()
    if process.wait():raise RuntimeError('Échec de la commande : '+shlex.join(map(str,args)))

def desktop_quote(value):
    # Desktop Entry Exec syntax differs from shell quoting.
    return '"'+value.replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')+'"'

def launcher_text(prefix):
    return '\n'.join(['#!/usr/bin/env bash','set -euo pipefail',
        'install_root='+shlex.quote(str(prefix)),
        'export GSETTINGS_SCHEMA_DIR="$install_root/share/glib-2.0/schemas"',
        'export LD_LIBRARY_PATH="$install_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"',
        'export XDG_DATA_DIRS="$install_root/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"',
        'exec "$install_root/bin/nautilus" "$@"',''])

def desktop_text(paths):
    return '\n'.join(['[Desktop Entry]','Type=Application','Name=Nautilus — Groupes (test)',
        'Comment=Version expérimentale : groupes repliables par type',
        'Exec='+desktop_quote(str(paths['launcher']))+' --new-window %U',
        'Icon='+str(paths['prefix']/'share/icons/hicolor/scalable/apps'/f'{APP_ID}.svg'),
        'Terminal=false','DBusActivatable=false','Categories=Utility;FileManager;',
        'StartupNotify=true',''])

def verify_sources():
    manifest=PACKAGE/'SHA256SUMS'
    if not manifest.exists():raise RuntimeError('SHA256SUMS absent : archive incomplète.')
    for line in manifest.read_text().splitlines():
        expected,name=line.split('  ',1)
        rel=pathlib.PurePosixPath(name)
        if rel.is_absolute() or '..' in rel.parts:raise RuntimeError('Chemin de manifeste invalide.')
        p=PACKAGE/rel
        if not p.is_file() or digest(p)!=expected:raise RuntimeError('Fichier absent ou modifié : '+name)
    return PACKAGE/'src-work/nautilus-50.3'

def preflight(paths):
    for name in ('prefix','launcher','desktop'):
        p=paths[name]
        if p.exists() or p.is_symlink():
            raise RuntimeError(f'{p} existe déjà. Rien ne sera écrasé. Désinstaller cette version avant de la reconstruire.')

def publish(stage_prefix,paths):
    """Publish only after build/tests; reserve ownership and roll back our files on error."""
    preflight(paths)
    for name in ('prefix','launcher','desktop'):paths[name].parent.mkdir(parents=True,exist_ok=True)
    created=[];moved=False
    try:
        # mkdir is exclusive: never rename over an unrelated empty directory.
        paths['prefix'].mkdir()
        created.append(paths['prefix'])
        for p in stage_prefix.iterdir():shutil.move(str(p),str(paths['prefix']/p.name))
        moved=True
        for name,text in [('launcher',launcher_text(paths['prefix'])),('desktop',desktop_text(paths))]:
            with paths[name].open('x',encoding='utf-8') as f:f.write(text)
            created.append(paths[name])
        paths['launcher'].chmod(0o755)
        record={'application_id':APP_ID,'format':1,'created_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),
                'launcher_sha256':digest(paths['launcher']),'desktop_sha256':digest(paths['desktop'])}
        (paths['prefix']/MARKER).write_text(json.dumps(record,indent=2)+'\n')
    except Exception:
        for p in reversed(created):
            if p.is_dir():shutil.rmtree(p)
            else:p.unlink(missing_ok=True)
        raise

def running_binary(prefix):
    target=str(prefix/'bin/nautilus')
    for p in pathlib.Path('/proc').glob('[0-9]*/exe'):
        try:
            if os.readlink(p)==target:return True
        except OSError:pass
    return False

def uninstall_paths(paths):
    prefix=paths['prefix'];marker=prefix/MARKER
    if prefix.is_symlink() or not marker.is_file():raise RuntimeError('Marqueur de propriété absent ; aucune suppression effectuée.')
    data=json.loads(marker.read_text())
    if data.get('application_id')!=APP_ID or data.get('format')!=1:raise RuntimeError('Marqueur non reconnu ; aucune suppression effectuée.')
    for name in ('launcher','desktop'):
        p=paths[name]
        if p.is_symlink() or (p.exists() and digest(p)!=data[name+'_sha256']):
            raise RuntimeError(f'{p} a été modifié ; aucune suppression effectuée.')
    if running_binary(prefix):raise RuntimeError('Fermer Nautilus — Groupes (test), puis relancer la désinstallation.')
    for name in ('desktop','launcher'):paths[name].unlink(missing_ok=True)
    shutil.rmtree(prefix)

def check_fedora():
    values={}
    for line in pathlib.Path('/etc/os-release').read_text().splitlines():
        if '=' in line:
            k,v=line.split('=',1);values[k]=v.strip('"')
    if values.get('ID')!='fedora' or values.get('VERSION_ID')!='44':
        raise RuntimeError('Cet installateur cible Fedora 44. Arrêt avant toute modification.')
    if os.geteuid()==0:raise RuntimeError('Lancer ce script sans sudo. Il demandera sudo uniquement pour les dépendances.')

def install(paths,skip_dependencies=False):
    check_fedora();source=verify_sources();preflight(paths)
    paths['state'].mkdir(parents=True,exist_ok=True)
    paths['cache'].mkdir(parents=True,exist_ok=True)
    with (paths['state']/'installation.log').open('w') as log:
        if not skip_dependencies:
            command(['sudo','dnf','install','dnf5-plugins','gcc','meson','ninja-build','pkgconf-pkg-config','python3'],log)
            command(['sudo','dnf','builddep','nautilus'],log)
        for name in ['meson','cc','pkg-config','glib-compile-schemas']:
            if not shutil.which(name):raise RuntimeError(f'Dépendance absente : {name}')
        command(['pkg-config','--atleast-version=4.20','gtk4'],log)
        command(['pkg-config','--atleast-version=2.84','glib-2.0'],log)
        # Same preserved tests as the development build, now against Fedora GTK.
        command(['python3',PACKAGE/'tests/test_mime_ownership.py',source],log)
        command(['python3',PACKAGE/'tests/test_group_persistence_grid.py',source],log)
        command(['bash',PACKAGE/'tests/run.sh',source],log)
        command(['python3',PACKAGE/'tests/test_settings_isolation.py'],log)
        work=pathlib.Path(tempfile.mkdtemp(prefix='build-',dir=paths['cache']))
        build=work/'build';staging=work/'stage'
        try:
            command(['meson','setup',build,source,'--prefix',paths['prefix'],'--libdir','lib',
                     '-Dprofile=Devel','-Dtests=none','-Dextensions=false','-Dintrospection=false','--buildtype=debugoptimized'],log)
            command(['meson','compile','-C',build,'-j',str(min(os.cpu_count() or 2,2))],log)
            command(['meson','install','-C',build,'--no-rebuild','--destdir',staging],log)
            staged=staging/paths['prefix'].relative_to('/')
            command(['glib-compile-schemas',staged/'share/glib-2.0/schemas'],log)
            if not (staged/'bin/nautilus').is_file():raise RuntimeError('Binaire non créé ; installation annulée.')
            # These service registrations are unnecessary for a manually launched test build.
            for relative in ['share/dbus-1','share/gnome-shell','share/applications']:
                p=staged/relative
                if p.exists():shutil.rmtree(p)
            publish(staged,paths)
        except Exception:
            print(f'Compilation ou installation interrompue. Dossier de diagnostic conservé : {work}',file=sys.stderr)
            raise
        else:shutil.rmtree(work)
    print('Installation terminée. Ouvrir « Nautilus — Groupes (test) » depuis les applications.')
    print('Lanceur : '+str(paths['launcher']))
    print('Le Nautilus Fedora et les associations de fichiers ne sont pas remplacés.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['install','uninstall','check'])
    parser.add_argument('--sans-dependances',action='store_true',help='Réutiliser des dépendances déjà installées.')
    args=parser.parse_args();paths=locations(pathlib.Path.home())
    try:
        if args.action=='check':verify_sources();print('Intégrité de tous les fichiers vérifiée.');return
        if args.action=='install':install(paths,args.sans_dependances)
        else:
            if os.geteuid()==0:raise RuntimeError('Lancer sans sudo, depuis le compte ayant installé le prototype.')
            uninstall_paths(paths)
            if shutil.which('dconf'):command(['dconf','reset','-f','/org/gnome/nautilus-groupes-test/'])
            print('Version de test désinstallée. Journaux de diagnostic et dépendances de compilation conservés.')
    except (OSError,RuntimeError,ValueError,KeyError) as exc:
        print('ARRÊT : '+str(exc),file=sys.stderr);sys.exit(1)
if __name__=='__main__':main()

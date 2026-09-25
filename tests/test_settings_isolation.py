#!/usr/bin/env python3
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
src = root / 'src-work' / 'nautilus-50.3'
schema_file = src / 'data' / 'org.gnome.nautilus.gschema.xml'
global_prefs = (src / 'src' / 'nautilus-global-preferences.c').read_text()
application = (src / 'src' / 'nautilus-application.c').read_text()

xml = ET.parse(schema_file).getroot()
schemas = {s.attrib['id']: s for s in xml.findall('schema')}
private_id = 'org.gnome.Nautilus.GroupesTest.FileChooser'
assert private_id in schemas, 'private file chooser schema missing'
private = schemas[private_id]
assert private.attrib.get('path') == '/org/gnome/nautilus-groupes-test/file-chooser/'
keys = {k.attrib['name']: k for k in private.findall('key')}
assert set(keys) == {'show-hidden', 'sort-directories-first'}
assert keys['show-hidden'].attrib.get('type') == 'b'
assert keys['sort-directories-first'].attrib.get('type') == 'b'

prefs = schemas['org.gnome.nautilus.preferences']
defaults = {k.attrib['name']: (k.findtext('default') or '').strip() for k in prefs.findall('key')}
assert defaults['default-sort-order'] == "'name'"
assert defaults['default-sort-in-reverse-order'] == 'false'
assert defaults['default-folder-viewer'] == "'list-view'"

assert f'g_settings_new ("{private_id}")' in global_prefs
assert '/org/gnome/nautilus-groupes-test/file-chooser/' not in global_prefs
assert 'g_settings_new ("org.gtk.gtk4.Settings.FileChooser")' in application
assert 'g_settings_new_with_path ("org.gtk.gtk4.Settings.FileChooser"' not in application
print('OK: private GSettings schema, no invalid GTK4 relocation, name/ascending list defaults.')

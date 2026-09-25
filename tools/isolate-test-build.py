#!/usr/bin/env python3
from pathlib import Path
import re

r=Path(__file__).resolve().parents[1]/'src-work/nautilus-50.3'

def replace(name, old, new):
    p=r/name
    s=p.read_text()
    assert old in s, (name, old)
    p.write_text(s.replace(old, new))

replace('meson.build', "app_id_suffix = '.Devel'", "app_id_suffix = '.GroupesTest'")
replace('meson.build', "name_suffix = ' (Development Snapshot)'", "name_suffix = ' (Groupes - test)'")
replace('src/nautilus-freedesktop-dbus.h', '"org.freedesktop.FileManager1"', '"org.gnome.Nautilus.GroupesTest.FileManager1"')
replace('data/org.freedesktop.FileManager1.service.in', 'Name=org.freedesktop.FileManager1', 'Name=org.gnome.Nautilus.GroupesTest.FileManager1')
replace('data/org.gnome.nautilus.gschema.xml', 'path="/org/gnome/nautilus/', 'path="/org/gnome/nautilus-groupes-test/')

p=r/'data/org.gnome.nautilus.gschema.xml'
s=p.read_text()
schema_anchor='  <schema path="/org/gnome/nautilus-groupes-test/preferences/" id="org.gnome.nautilus.preferences" gettext-domain="nautilus">\n'
private_schema="""  <schema path="/org/gnome/nautilus-groupes-test/file-chooser/" id="org.gnome.Nautilus.GroupesTest.FileChooser">
    <key type="b" name="show-hidden">
      <default>false</default>
      <summary>Show hidden files in Nautilus Groupes</summary>
    </key>
    <key type="b" name="sort-directories-first">
      <default>true</default>
      <summary>Show folders before files in Nautilus Groupes</summary>
    </key>
  </schema>

"""
assert schema_anchor in s
s=s.replace(schema_anchor, private_schema+schema_anchor, 1)
s,n=re.subn(r'(<key\b[^>]*name="default-folder-viewer"[^>]*>.*?<default>).*?(</default>)',
            lambda m:m[1]+"'list-view'"+m[2], s, count=1, flags=re.S)
assert n == 1
p.write_text(s)

replace('src/nautilus-global-preferences.c',
        '''    /* Some settings such as show hidden files are shared between Nautilus and GTK file chooser */
    gtk_filechooser_preferences = g_settings_new_with_path ("org.gtk.gtk4.Settings.FileChooser",
                                                            "/org/gtk/gtk4/settings/file-chooser/");''',
        '''    /* Keep the two file-chooser preferences used by this prototype private,
     * so changing them here does not alter Fedora Nautilus or GTK dialogs. */
    gtk_filechooser_preferences = g_settings_new ("org.gnome.Nautilus.GroupesTest.FileChooser");''')

p=r/'src/nautilus-application.c'
s=p.read_text()
start=s.index('static void\nmaybe_migrate_gtk_filechooser_preferences (void)')
end=s.index('static void\nnautilus_application_identify_to_portal', start)
newfunc="""static void
maybe_migrate_gtk_filechooser_preferences (void)
{
    if (!g_settings_get_boolean (nautilus_preferences, NAUTILUS_PREFERENCES_MIGRATED_GTK_SETTINGS))
    {
        GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
        g_autoptr (GSettingsSchema) schema = NULL;
        g_autoptr (GSettings) source_settings = NULL;

        /* Seed the private prototype settings from the user's current GTK 4
         * preferences, but do not keep sharing the global GTK settings. */
        schema = g_settings_schema_source_lookup (source,
                                                  "org.gtk.gtk4.Settings.FileChooser",
                                                  FALSE);
        if (schema != NULL)
        {
            source_settings = g_settings_new ("org.gtk.gtk4.Settings.FileChooser");
        }
        else
        {
            /* Fallback for unusual systems where only the GTK 3 schema exists. */
            g_clear_pointer (&schema, g_settings_schema_unref);
            schema = g_settings_schema_source_lookup (source,
                                                      "org.gtk.Settings.FileChooser",
                                                      FALSE);
            if (schema != NULL)
            {
                source_settings = g_settings_new_with_path ("org.gtk.Settings.FileChooser",
                                                            "/org/gtk/settings/file-chooser/");
            }
        }

        if (source_settings != NULL)
        {
            g_settings_set_boolean (gtk_filechooser_preferences,
                                    NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST,
                                    g_settings_get_boolean (source_settings,
                                                            NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST));
            g_settings_set_boolean (gtk_filechooser_preferences,
                                    NAUTILUS_PREFERENCES_SHOW_HIDDEN_FILES,
                                    g_settings_get_boolean (source_settings,
                                                            NAUTILUS_PREFERENCES_SHOW_HIDDEN_FILES));
        }

        g_settings_set_boolean (nautilus_preferences,
                                NAUTILUS_PREFERENCES_MIGRATED_GTK_SETTINGS,
                                TRUE);
    }
}

"""
p.write_text(s[:start]+newfunc+s[end:])

replace('src/nautilus-metadata.h', '"nautilus-icon-view', '"nautilus-groupes-test-icon-view')
replace('src/nautilus-metadata.h', '"nautilus-list-view', '"nautilus-groupes-test-list-view')
print('Identite DBus et preferences privees configurees.')

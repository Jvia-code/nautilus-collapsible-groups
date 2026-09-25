#!/usr/bin/env python3
"""Reconstruct the previously developed patch from the conversation's code.
Run ONLY on a pristine Nautilus 50.3 source tree. Fails if anchors do not match.
"""
from pathlib import Path
import shutil
package_root=Path(__file__).resolve().parents[1]
root=package_root/'src-work'/'nautilus-50.3'
def edit(name,old,new):
 p=root/name;s=p.read_text();assert old in s,(name,old[:80]);p.write_text(s.replace(old,new))
def append(name,s):
 p=root/name;p.write_text(p.read_text()+s)
# Keep the experimental build isolated from Fedora Nautilus.
edit('meson.build', "if get_option('profile') == 'Devel'\n  app_id_suffix = '.Devel'\n  name_suffix = ' (Development Snapshot)'\nendif", "if get_option('profile') == 'Devel'\n  app_id_suffix = '.GroupesTest'\n  name_suffix = ' (Groupes - test)'\nendif")
edit('data/org.freedesktop.FileManager1.service.in',
     'Name=org.freedesktop.FileManager1',
     'Name=org.gnome.Nautilus.GroupesTest.FileManager1')
edit('src/nautilus-freedesktop-dbus.h',
     '#define NAUTILUS_FDO_DBUS_IFACE "org.freedesktop.FileManager1"\n#define NAUTILUS_FDO_DBUS_NAME  "org.freedesktop.FileManager1"',
     '#define NAUTILUS_FDO_DBUS_IFACE "org.gnome.Nautilus.GroupesTest.FileManager1"\n#define NAUTILUS_FDO_DBUS_NAME  "org.gnome.Nautilus.GroupesTest.FileManager1"')

schema='data/org.gnome.nautilus.gschema.xml'
edit(schema, '<schema path="/org/gnome/nautilus/" id="org.gnome.nautilus"',
             '<schema path="/org/gnome/nautilus-groupes-test/" id="org.gnome.nautilus"')
edit(schema, '<schema path="/org/gnome/nautilus/preferences/" id="org.gnome.nautilus.preferences"', '''<schema path="/org/gnome/nautilus-groupes-test/file-chooser/" id="org.gnome.Nautilus.GroupesTest.FileChooser">
    <key type="b" name="show-hidden">
      <default>false</default>
      <summary>Show hidden files in Nautilus Groupes</summary>
    </key>
    <key type="b" name="sort-directories-first">
      <default>true</default>
      <summary>Show folders before files in Nautilus Groupes</summary>
    </key>
  </schema>

  <schema path="/org/gnome/nautilus-groupes-test/preferences/" id="org.gnome.nautilus.preferences"''')
edit(schema, '<schema path="/org/gnome/nautilus/compression/" id="org.gnome.nautilus.compression"',
             '<schema path="/org/gnome/nautilus-groupes-test/compression/" id="org.gnome.nautilus.compression"')
edit(schema, '<schema path="/org/gnome/nautilus/icon-view/" id="org.gnome.nautilus.icon-view"',
             '<schema path="/org/gnome/nautilus-groupes-test/icon-view/" id="org.gnome.nautilus.icon-view"')
edit(schema, '<schema path="/org/gnome/nautilus/list-view/" id="org.gnome.nautilus.list-view"',
             '<schema path="/org/gnome/nautilus-groupes-test/list-view/" id="org.gnome.nautilus.list-view"')
edit(schema, '<schema path="/org/gnome/nautilus/window-state/" id="org.gnome.nautilus.window-state"',
             '<schema path="/org/gnome/nautilus-groupes-test/window-state/" id="org.gnome.nautilus.window-state"')
edit(schema, "<default>'icon-view'</default>", "<default>'list-view'</default>")

edit('src/nautilus-global-preferences.c', '''    /* Some settings such as show hidden files are shared between Nautilus and GTK file chooser */
    gtk_filechooser_preferences = g_settings_new_with_path ("org.gtk.gtk4.Settings.FileChooser",
                                                            "/org/gtk/gtk4/settings/file-chooser/");''', '''    /* Keep the two file-chooser preferences used by this prototype private,
     * so changing them here does not alter Fedora Nautilus or GTK dialogs. */
    gtk_filechooser_preferences = g_settings_new ("org.gnome.Nautilus.GroupesTest.FileChooser");''')

edit('src/nautilus-application.c', '''        g_autoptr (GSettingsSchema) schema = NULL;

        /* We don't depend on GTK 3. Check whether its schema is installed. */
        schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
                                                  "org.gtk.Settings.FileChooser",
                                                  FALSE);
        if (schema != NULL)
        {
            g_autoptr (GSettings) gtk3_settings = NULL;

            gtk3_settings = g_settings_new_with_path ("org.gtk.Settings.FileChooser",
                                                      "/org/gtk/settings/file-chooser/");
            g_settings_set_boolean (gtk_filechooser_preferences,
                                    NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST,
                                    g_settings_get_boolean (gtk3_settings,
                                                            NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST));
            g_settings_set_boolean (gtk_filechooser_preferences,
                                    NAUTILUS_PREFERENCES_SHOW_HIDDEN_FILES,
                                    g_settings_get_boolean (gtk3_settings,
                                                            NAUTILUS_PREFERENCES_SHOW_HIDDEN_FILES));
        }
        g_settings_set_boolean (nautilus_preferences,
                                NAUTILUS_PREFERENCES_MIGRATED_GTK_SETTINGS,
                                TRUE);''', '''        GSettingsSchemaSource *source = g_settings_schema_source_get_default ();
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
                                TRUE);''')

edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_ICON_VIEW_SORT_BY          \t"nautilus-icon-view-sort-by"',
     '#define NAUTILUS_METADATA_KEY_ICON_VIEW_SORT_BY          \t"nautilus-groupes-test-icon-view-sort-by"')
edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_ICON_VIEW_SORT_REVERSED    \t"nautilus-icon-view-sort-reversed"',
     '#define NAUTILUS_METADATA_KEY_ICON_VIEW_SORT_REVERSED    \t"nautilus-groupes-test-icon-view-sort-reversed"')
edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_SORT_COLUMN      \t"nautilus-list-view-sort-column"',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_SORT_COLUMN      \t"nautilus-groupes-test-list-view-sort-column"')
edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_SORT_REVERSED    \t"nautilus-list-view-sort-reversed"',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_SORT_REVERSED    \t"nautilus-groupes-test-list-view-sort-reversed"')
edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS    \t"nautilus-list-view-visible-columns"',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_VISIBLE_COLUMNS    \t"nautilus-groupes-test-list-view-visible-columns"')
edit('src/nautilus-metadata.h',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_COLUMN_ORDER    \t"nautilus-list-view-column-order"',
     '#define NAUTILUS_METADATA_KEY_LIST_VIEW_COLUMN_ORDER    \t"nautilus-groupes-test-list-view-column-order"')
edit('src/nautilus-view-item.c','    GtkWidget *item_ui;','    GtkWidget *item_ui;\n    char *group_key;\n    char *group_title;\n    GListStore *group_children;')
edit('src/nautilus-view-item.c','    PROP_FILE,','    PROP_FILE,\n    PROP_GROUP_TITLE,')
edit('src/nautilus-view-item.c','    g_clear_object (&self->file);','    g_clear_object (&self->file);\n    g_clear_object (&self->group_children);\n    g_free (self->group_key);\n    g_free (self->group_title);')
edit('src/nautilus-view-item.c','        case PROP_FILE:\n        {\n            g_value_set_object','        case PROP_GROUP_TITLE:\n            g_value_set_string (value, self->group_title);\n            break;\n        case PROP_FILE:\n        {\n            g_value_set_object')
edit('src/nautilus-view-item.c','    g_object_class_install_properties (object_class, N_PROPS, properties);','    properties[PROP_GROUP_TITLE] = g_param_spec_string ("group-title", NULL, NULL, NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);\n    g_object_class_install_properties (object_class, N_PROPS, properties);')
append('src/nautilus-view-item.c',r'''
/* Non-file presentation headers. Never enter the filesystem item map. */
NautilusViewItem *
nautilus_view_item_new_group (const char *key)
{
    NautilusViewItem *self = nautilus_view_item_new (NULL);
    self->group_key = g_strdup (key);
    self->group_children = g_list_store_new (NAUTILUS_TYPE_VIEW_ITEM);
    return self;
}
gboolean
nautilus_view_item_is_group (NautilusViewItem *self)
{
    return self != NULL && self->group_key != NULL;
}
const char *
nautilus_view_item_get_group_key (NautilusViewItem *self)
{
    return self->group_key;
}
GListStore *
nautilus_view_item_get_group_children (NautilusViewItem *self)
{
    return self->group_children;
}
void
nautilus_view_item_set_group_title (NautilusViewItem *self, const char *title)
{
    if (g_strcmp0 (self->group_title, title) == 0) return;
    g_free (self->group_title);
    self->group_title = g_strdup (title);
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_GROUP_TITLE]);
}
''')
edit('src/nautilus-view-item.h','G_END_DECLS','''NautilusViewItem *nautilus_view_item_new_group (const char *key);
gboolean nautilus_view_item_is_group (NautilusViewItem *self);
const char *nautilus_view_item_get_group_key (NautilusViewItem *self);
GListStore *nautilus_view_item_get_group_children (NautilusViewItem *self);
void nautilus_view_item_set_group_title (NautilusViewItem *self, const char *title);
G_END_DECLS''')
edit('src/nautilus-view-model.c','    GtkSelectionModel *selection_model;','''    GtkSelectionModel *selection_model;
    GListStore *group_roots;
    GtkTreeListModel *group_tree;
    GHashTable *groups;
    GHashTable *collapsed_groups;
    gboolean group_by_type;
    gboolean rebuilding_groups;''')
edit('src/nautilus-view-model.c','    if (self->tree_model == NULL)','    if (self->selection_model == NULL)')
edit('src/nautilus-view-model.c','return g_list_model_get_n_items (G_LIST_MODEL (self->tree_model));','return g_list_model_get_n_items (G_LIST_MODEL (self->selection_model));')
edit('src/nautilus-view-model.c','    if (self->sort_model == NULL)','    if (self->selection_model == NULL)')
edit('src/nautilus-view-model.c','return g_list_model_get_item (G_LIST_MODEL (self->sort_model), position);','return g_list_model_get_item (G_LIST_MODEL (self->selection_model), position);')
edit('src/nautilus-view-model.c','    gtk_section_model_get_section (GTK_SECTION_MODEL (self->sort_model), position, out_start, out_end);','''    if (self->group_by_type)
    {
        guint n = g_list_model_get_n_items (G_LIST_MODEL (self));
        *out_start = position < n ? 0 : n;
        *out_end = position < n ? n : G_MAXUINT;
    }
    else
        gtk_section_model_get_section (GTK_SECTION_MODEL (self->sort_model), position, out_start, out_end);''')
edit('src/nautilus-view-model.c','    return gtk_selection_model_select_item (self->selection_model, position, unselect_rest);','''    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), position);
    g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;
    if (item == NULL || nautilus_view_item_is_group (item)) return FALSE;
    return gtk_selection_model_select_item (self->selection_model, position, unselect_rest);''')
edit('src/nautilus-view-model.c','    return gtk_selection_model_set_selection (self->selection_model, selected, mask);','''    g_autoptr (GtkBitset) safe = gtk_bitset_copy (selected);
    if (self->group_by_type)
    {
        GtkBitsetIter iter;
        guint position;
        for (gtk_bitset_iter_init_first (&iter, selected, &position);
             gtk_bitset_iter_is_valid (&iter); gtk_bitset_iter_next (&iter, &position))
        {
            g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), position);
            g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;
            if (item == NULL || nautilus_view_item_is_group (item)) gtk_bitset_remove (safe, position);
        }
    }
    return gtk_selection_model_set_selection (self->selection_model, safe, mask);''')
edit('src/nautilus-view-model.c','static void\ndispose (GObject *object)','''static void rebuild_type_groups (NautilusViewModel *self);
static void on_source_items_changed (GListModel *source, guint position, guint removed, guint added, NautilusViewModel *self);
static void on_source_sections_changed (GtkSectionModel *source, guint position, guint n_items, NautilusViewModel *self);

static void
dispose (GObject *object)''')
edit('src/nautilus-view-model.c','        g_object_unref (self->selection_model);','        g_signal_handlers_disconnect_by_func (self->selection_model, g_list_model_items_changed, self);\n        g_object_unref (self->selection_model);')
edit('src/nautilus-view-model.c','        g_object_unref (self->sort_model);','''        g_signal_handlers_disconnect_by_func (self->sort_model, on_source_items_changed, self);
        g_signal_handlers_disconnect_by_func (self->sort_model, on_source_sections_changed, self);
        g_object_unref (self->sort_model);''')
edit('src/nautilus-view-model.c','    g_clear_object (&self->tree_model);','''    g_clear_object (&self->group_tree);
    g_clear_object (&self->group_roots);
    g_clear_pointer (&self->groups, g_hash_table_unref);
    g_clear_pointer (&self->collapsed_groups, g_hash_table_unref);
    g_clear_object (&self->tree_model);''')
edit('src/nautilus-view-model.c','static void\nnautilus_view_model_init (NautilusViewModel *self)\n{\n}','''static void
nautilus_view_model_init (NautilusViewModel *self)
{
    self->collapsed_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}''')
edit('src/nautilus-view-model.c','    g_signal_connect_swapped (self->sort_model, "items-changed",\n                              G_CALLBACK (g_list_model_items_changed), self);','''    g_signal_connect (self->sort_model, "items-changed", G_CALLBACK (on_source_items_changed), self);
    g_signal_connect_swapped (self->selection_model, "items-changed", G_CALLBACK (g_list_model_items_changed), self);''')
edit('src/nautilus-view-model.c','    g_signal_connect_swapped (self->sort_model, "sections-changed",\n                              G_CALLBACK (gtk_section_model_sections_changed), self);','    g_signal_connect (self->sort_model, "sections-changed", G_CALLBACK (on_source_sections_changed), self);')
edit('src/nautilus-view-model.c','    if (sorter != NULL)\n    {\n        gtk_sorter_changed (sorter, GTK_SORTER_CHANGE_DIFFERENT);\n    }','''    if (sorter != NULL)
    {
        gtk_sorter_changed (sorter, GTK_SORTER_CHANGE_DIFFERENT);
    }
    if (self->group_by_type) rebuild_type_groups (self);''')
edit('src/nautilus-view-model.h','G_END_DECLS','''void nautilus_view_model_set_group_by_type (NautilusViewModel *self, gboolean enabled);
gboolean nautilus_view_model_get_group_by_type (NautilusViewModel *self);
void nautilus_view_model_set_collapsed_groups (NautilusViewModel *self, const char * const *keys);
char **nautilus_view_model_dup_collapsed_groups (NautilusViewModel *self);
G_END_DECLS''')
append('src/nautilus-view-model.c',(Path(__file__).parent/'group-model.inc.c').read_text())
# Grid view grouping. GtkGridView has no full-width header factory, so each
# group header is represented as a dedicated non-selectable grid tile.
edit('src/nautilus-grid-view.c','    gboolean reversed;','    gboolean reversed;\n    gboolean group_by_type;')
edit('src/nautilus-grid-view.c','static const NautilusViewInfo grid_view_info =',(Path(__file__).parent/'group-grid.inc.c').read_text()+'\nstatic const NautilusViewInfo grid_view_info =')
edit('src/nautilus-grid-view.c','''    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);

    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);''','''    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), position);
    g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;

    if (item != NULL && nautilus_view_item_is_group (item))
    {
        gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
        return;
    }

    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);''')
edit('src/nautilus-grid-view.c','''    cell = gtk_list_item_get_child (listitem);
    item = get_view_item (listitem);
    g_return_if_fail (item != NULL);

    nautilus_view_item_set_item_ui (item, cell);''','''    item = get_view_item (listitem);
    g_return_if_fail (item != NULL);

    GtkWidget *stack = gtk_list_item_get_child (listitem);
    gboolean is_group = nautilus_view_item_is_group (item);
    gtk_stack_set_visible_child_name (GTK_STACK (stack), is_group ? "group" : "file");
    gtk_list_item_set_selectable (listitem, !is_group);
    gtk_list_item_set_activatable (listitem, !is_group);

    clear_grid_group_binding (listitem);
    if (is_group)
    {
        GtkTreeExpander *expander = grid_group_expander (listitem);
        gtk_tree_expander_set_list_row (expander, GTK_TREE_LIST_ROW (gtk_list_item_get_item (listitem)));
        GBinding *binding = g_object_bind_property (item, "group-title",
                                                   gtk_tree_expander_get_child (expander), "label",
                                                   G_BINDING_SYNC_CREATE);
        g_object_set_data_full (G_OBJECT (listitem), "group-title-binding", g_object_ref (binding), g_object_unref);
        gtk_list_item_set_accessible_label (listitem, nautilus_view_item_get_group_key (item));
        return;
    }

    cell = grid_file_cell (listitem);
    nautilus_view_item_set_item_ui (item, cell);''')
edit('src/nautilus-grid-view.c','''    item = get_view_item (listitem);

    /* item may be NULL when row has just been destroyed. */
    if (item != NULL)
    {
        nautilus_view_item_set_item_ui (item, NULL);
    }''','''    item = get_view_item (listitem);
    clear_grid_group_binding (listitem);

    /* item may be NULL when row has just been destroyed. */
    if (item != NULL && !nautilus_view_item_is_group (item))
    {
        nautilus_view_item_set_item_ui (item, NULL);
    }''')
edit('src/nautilus-grid-view.c','''    cell = nautilus_grid_cell_new (NAUTILUS_LIST_BASE (self));
    gtk_list_item_set_child (listitem, GTK_WIDGET (cell));
    setup_cell_common (G_OBJECT (listitem), NAUTILUS_VIEW_CELL (cell));''','''    cell = nautilus_grid_cell_new (NAUTILUS_LIST_BASE (self));
    GtkWidget *stack = gtk_stack_new ();
    gtk_stack_set_hhomogeneous (GTK_STACK (stack), FALSE);
    gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
    gtk_stack_add_named (GTK_STACK (stack), GTK_WIDGET (cell), "file");

    GtkWidget *expander = gtk_tree_expander_new ();
    GtkWidget *label = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class (label, "heading");
    gtk_widget_set_margin_top (expander, 12);
    gtk_widget_set_margin_bottom (expander, 12);
    gtk_widget_set_margin_start (expander, 6);
    gtk_widget_set_margin_end (expander, 6);
    gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), label);
    gtk_stack_add_named (GTK_STACK (stack), expander, "group");
    g_object_set_data (G_OBJECT (listitem), "group-expander", expander);
    gtk_list_item_set_child (listitem, stack);

    setup_cell_common (G_OBJECT (listitem), NAUTILUS_VIEW_CELL (cell));''')
edit('src/nautilus-grid-view.c','''    if (model != NULL)
    {
        gtk_grid_view_set_enable_rubberband (GTK_GRID_VIEW (self->view_ui),
                                             !nautilus_view_model_get_single_selection (model));
    }''','''    if (model != NULL)
    {
        nautilus_view_model_set_group_by_type (model, self->group_by_type);
        gtk_grid_view_set_enable_rubberband (GTK_GRID_VIEW (self->view_ui),
                                             !nautilus_view_model_get_single_selection (model));
    }''')
edit('src/nautilus-grid-view.c','    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-grid-view");','    self->group_by_type = TRUE;\n    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-grid-view");')
append('src/nautilus-grid-view.c','''
void
nautilus_grid_view_set_group_by_type (NautilusGridView *self, gboolean enabled)
{
    self->group_by_type = enabled;
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    if (model != NULL) nautilus_view_model_set_group_by_type (model, enabled);
}
''')
edit('src/nautilus-grid-view.h','G_END_DECLS','void nautilus_grid_view_set_group_by_type (NautilusGridView *self, gboolean enabled);\nG_END_DECLS')

# Never bind a non-file group header to an existing Nautilus file cell.
edit('src/nautilus-list-base.c','void\nsetup_cell_common (GObject','''static NautilusViewItem *
file_item_expression (gpointer unused, NautilusViewItem *item)
{
    return item != NULL && !nautilus_view_item_is_group (item) ? g_object_ref (item) : NULL;
}

void
setup_cell_common (GObject''')
edit('src/nautilus-list-base.c','    gtk_expression_bind (expression, cell, "item", listitem);','''    expression = gtk_cclosure_expression_new (NAUTILUS_TYPE_VIEW_ITEM, NULL, 1, &expression,
                                              G_CALLBACK (file_item_expression), NULL, NULL);
    gtk_expression_bind (expression, cell, "item", listitem);''')
edit('src/nautilus-list-view.c','    GtkSorter *view_model_sorter;','    GtkSorter *view_model_sorter;\n    gboolean group_by_type;')
edit('src/nautilus-list-view.c','static const NautilusViewInfo list_view_info =',(Path(__file__).parent/'group-cells.inc.c').read_text()+'\nstatic const NautilusViewInfo list_view_info =')
edit('src/nautilus-list-view.c','    g_signal_connect (row_factory, "setup", G_CALLBACK (setup_row), self);','    g_signal_connect (row_factory, "setup", G_CALLBACK (setup_row), self);\n    g_signal_connect (row_factory, "bind", G_CALLBACK (bind_group_row), self);')
edit('src/nautilus-list-view.c','    cell = gtk_column_view_cell_get_child (listitem);\n    item = get_view_item (listitem);','    cell = file_cell (listitem);\n    item = get_view_item (listitem);\n    if (nautilus_view_item_is_group (item)) return;')
edit('src/nautilus-list-view.c','nautilus_view_item_set_item_ui (item, gtk_column_view_cell_get_child (listitem));','nautilus_view_item_set_item_ui (item, file_cell (listitem));')
edit('src/nautilus-list-view.c','    if (item == NULL)\n    {\n        /* The row is gone */','    if (item == NULL || nautilus_view_item_is_group (item))\n    {\n        /* The row is gone or is a non-file header. */')
edit('src/nautilus-list-view.c','        gtk_column_view_append_column (self->view_ui, view_column);','''        g_signal_connect (factory, "setup", G_CALLBACK (setup_group_wrapper), self);
        g_signal_connect (factory, "bind", G_CALLBACK (bind_group_wrapper), self);
        g_signal_connect (factory, "unbind", G_CALLBACK (unbind_group_wrapper), self);
        gtk_column_view_append_column (self->view_ui, view_column);''')
edit('src/nautilus-list-view.c','    self->expand_as_a_tree = g_settings_get_boolean (nautilus_list_view_preferences,\n                                                     NAUTILUS_PREFERENCES_LIST_VIEW_USE_TREE);','    /* Experimental version reserves the tree level for type groups. */\n    self->expand_as_a_tree = FALSE;')
edit('src/nautilus-list-view.c','        nautilus_view_model_set_sorter (model, self->view_model_sorter);','        nautilus_view_model_set_sorter (model, self->view_model_sorter);\n        nautilus_view_model_set_group_by_type (model, self->group_by_type);')
edit('src/nautilus-list-view.c','    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-list-view");','    self->group_by_type = TRUE;\n    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-list-view");')
edit('src/nautilus-list-view.c','            nautilus_view_model_set_sorter (model, NULL);','            nautilus_view_model_set_group_by_type (model, FALSE);\n            nautilus_view_model_set_sorter (model, NULL);')
edit('src/nautilus-list-view.c','    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);','''    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), position);
    g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;
    if (nautilus_view_item_is_group (item))
    {
        gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
        return;
    }
    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);''')
append('src/nautilus-list-view.c','''
void
nautilus_list_view_set_group_by_type (NautilusListView *self, gboolean enabled)
{
    self->group_by_type = enabled;
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    if (model != NULL) nautilus_view_model_set_group_by_type (model, enabled);
}
''')
edit('src/nautilus-list-view.h','G_END_DECLS','void nautilus_list_view_set_group_by_type (NautilusListView *self, gboolean enabled);\nG_END_DECLS')
edit('src/nautilus-metadata.h','#define NAUTILUS_METADATA_KEY_CUSTOM_ICON', '#define NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES         "nautilus-groupes-test-collapsed-types"\n\n#define NAUTILUS_METADATA_KEY_CUSTOM_ICON')
edit('src/nautilus-metadata.c','    NAUTILUS_METADATA_KEY_LIST_VIEW_COLUMN_ORDER,','    NAUTILUS_METADATA_KEY_LIST_VIEW_COLUMN_ORDER,\n    NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES,')
edit('src/nautilus-files-view.c','static void\nfiles_view_begin_loading (NautilusFilesView *self)','''static void
save_group_state_for_directory (NautilusFilesView *self)
{
    if (self->directory_as_file == NULL || self->model == NULL) return;

    g_auto (GStrv) collapsed = nautilus_view_model_dup_collapsed_groups (self->model);
    nautilus_file_set_metadata_list (self->directory_as_file,
                                     NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES,
                                     collapsed);
}

static void
load_group_state_for_directory (NautilusFilesView *self)
{
    g_auto (GStrv) collapsed = NULL;

    if (self->directory_as_file != NULL)
        collapsed = nautilus_file_get_metadata_list (self->directory_as_file,
                                                     NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES);

    nautilus_view_model_set_collapsed_groups (self->model, (const char * const *) collapsed);
}

static void
files_view_begin_loading (NautilusFilesView *self)''')
edit('src/nautilus-files-view.c','    update_sort_order_from_metadata_and_preferences (self);','    update_sort_order_from_metadata_and_preferences (self);\n    load_group_state_for_directory (self);')
# Save the old folder before directory_as_file is replaced during navigation.
edit('src/nautilus-files-view.c','    nautilus_file_unref (self->directory_as_file);\n    self->directory_as_file = nautilus_directory_get_corresponding_file (directory);','    save_group_state_for_directory (self);\n    nautilus_file_unref (self->directory_as_file);\n    self->directory_as_file = nautilus_directory_get_corresponding_file (directory);')
edit('src/nautilus-files-view.c','static void\naction_sort_order_changed','''static void
action_group_by_type (GSimpleAction *action, GVariant *value, gpointer user_data)
{
    NautilusFilesView *self = user_data;
    g_simple_action_set_state (action, value);
    if (NAUTILUS_IS_LIST_VIEW (self->list_base))
        nautilus_list_view_set_group_by_type (NAUTILUS_LIST_VIEW (self->list_base), g_variant_get_boolean (value));
    else if (NAUTILUS_IS_GRID_VIEW (self->list_base))
        nautilus_grid_view_set_group_by_type (NAUTILUS_GRID_VIEW (self->list_base), g_variant_get_boolean (value));
}

static void
action_sort_order_changed''')
edit('src/nautilus-files-view.c','    { .name = "visible-columns",','    { .name = "group-by-type", .state = "true", .change_state = action_group_by_type },\n    { .name = "visible-columns",')
edit('src/nautilus-files-view.c','    nautilus_list_base_set_model (self->list_base, self->model);','''    nautilus_list_base_set_model (self->list_base, self->model);
    GAction *group_action = g_action_map_lookup_action (G_ACTION_MAP (self->view_action_group), "group-by-type");
    gboolean grouping_supported = NAUTILUS_IS_LIST_VIEW (self->list_base) || NAUTILUS_IS_GRID_VIEW (self->list_base);
    g_simple_action_set_enabled (G_SIMPLE_ACTION (group_action), grouping_supported);
    if (grouping_supported)
    {
        g_autoptr (GVariant) state = g_action_get_state (group_action);
        gboolean enabled = g_variant_get_boolean (state);
        if (NAUTILUS_IS_LIST_VIEW (self->list_base))
            nautilus_list_view_set_group_by_type (NAUTILUS_LIST_VIEW (self->list_base), enabled);
        else
            nautilus_grid_view_set_group_by_type (NAUTILUS_GRID_VIEW (self->list_base), enabled);
    }
    else
        nautilus_view_model_set_group_by_type (self->model, FALSE);''')
edit('src/resources/menu/nautilus-toolbar-view-menu.ui','  <menu id="sort_section">','''  <menu id="sort_section">
    <item>
      <attribute name="action">view.group-by-type</attribute>
      <attribute name="label">Regrouper par type</attribute>
      <attribute name="hidden-when">action-disabled</attribute>
    </item>''')
edit('src/nautilus-files-view.c','        nautilus_list_base_set_cursor (self->list_base, 0, TRUE, TRUE);','''        guint n = g_list_model_get_n_items (G_LIST_MODEL (self->model));
        for (guint i = 0; i < n; i++)
        {
            g_autoptr (NautilusViewItem) item = get_view_item (G_LIST_MODEL (self->model), i);
            if (!nautilus_view_item_is_group (item))
            {
                nautilus_list_base_set_cursor (self->list_base, i, TRUE, TRUE);
                break;
            }
        }''')
edit('src/nautilus-files-view.c','        file = nautilus_view_item_get_file (item);\n\n        selected_files','        file = nautilus_view_item_get_file (item);\n        if (file == NULL || nautilus_view_item_is_group (item)) continue;\n\n        selected_files')
edit('src/nautilus-files-view.c','    self = NAUTILUS_FILES_VIEW (object);','    self = NAUTILUS_FILES_VIEW (object);\n\n    save_group_state_for_directory (self);')
# V3.3.2 Option B: grouped grid is a vertical GtkBox of groups, each owning a
# non-scrollable GtkFlowBox. The snapshot also initializes each NautilusGridCell
# icon size before binding its item, matching the native GtkGridView lifecycle.
snapshot_root = package_root / 'tools' / 'v332-snapshots' / 'src'
for name in (
    'nautilus-grid-view.c',
    'nautilus-list-base.c',
    'nautilus-list-base-private.h',
    'nautilus-view-model.c',
    'nautilus-view-model.h',
):
    source = snapshot_root / name
    assert source.is_file(), source
    shutil.copy2(source, root / 'src' / name)

append('src/resources/style.css', r'''
/* Grouped grid: each group uses GtkFlowBox, a non-scrollable height-for-width
 * layout. This avoids the oversized natural heights produced by nesting
 * GtkGridView (GtkScrollable) widgets inside the outer scrolling view. */
.nautilus-grid-view .grouped-grid flowbox.group-flow {
  padding: 4px 18px 8px;
  background: none;
}

.nautilus-grid-view .grouped-grid flowbox.group-flow > flowboxchild {
  padding: 0;
  border-radius: 12px;
}

.nautilus-grid-view .grouped-grid .group-block {
  margin: 0;
  padding: 0;
}
''')

print('Reconstruction des sources terminee (V3.3.2 FlowBox + cycle de vie cleanup).')

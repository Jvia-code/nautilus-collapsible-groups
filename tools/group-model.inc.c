/* SPDX-License-Identifier: GPL-3.0-or-later */
static GListModel *
create_group_children (gpointer item, gpointer data)
{
    GListStore *children = nautilus_view_item_get_group_children (item);
    return children ? G_LIST_MODEL (g_object_ref (children)) : NULL;
}

static char *
file_group_key (NautilusFile *file)
{
    if (nautilus_file_is_directory (file)) return g_strdup ("0:directories");

    /* Borrowed from NautilusFile: always duplicate before returning ownership. */
    const char *mime = nautilus_file_get_mime_type (file);
    return mime != NULL ? g_strdup (mime) : g_strdup ("application/octet-stream");
}

/* Minimal contiguous replacement preserves unaffected tree rows. */
static void
update_store (GListStore *store, GPtrArray *items)
{
    guint old_n = g_list_model_get_n_items (G_LIST_MODEL (store));
    guint start = 0, tail = 0;

    while (start < MIN (old_n, items->len))
    {
        g_autoptr (GObject) old = g_list_model_get_item (G_LIST_MODEL (store), start);
        if (old != items->pdata[start]) break;
        start++;
    }

    while (tail < MIN (old_n, items->len) - start)
    {
        g_autoptr (GObject) old = g_list_model_get_item (G_LIST_MODEL (store), old_n - tail - 1);
        if (old != items->pdata[items->len - tail - 1]) break;
        tail++;
    }

    if (old_n != items->len || start + tail != old_n)
        g_list_store_splice (store, start, old_n - start - tail,
                            items->len > start + tail ? items->pdata + start : NULL,
                            items->len - start - tail);
}

static GHashTable *
remember_selected_items (NautilusViewModel *self)
{
    GHashTable *selected = g_hash_table_new_full (g_direct_hash, g_direct_equal, g_object_unref, NULL);
    g_autoptr (GtkBitset) bits = gtk_selection_model_get_selection (self->selection_model);
    GtkBitsetIter iter;
    guint pos;

    for (gtk_bitset_iter_init_first (&iter, bits, &pos);
         gtk_bitset_iter_is_valid (&iter); gtk_bitset_iter_next (&iter, &pos))
    {
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), pos);
        if (row != NULL) g_hash_table_add (selected, gtk_tree_list_row_get_item (row));
    }

    return selected;
}

static void
restore_selected_items (NautilusViewModel *self, GHashTable *selected)
{
    guint n = g_list_model_get_n_items (G_LIST_MODEL (self));
    g_autoptr (GtkBitset) bits = gtk_bitset_new_empty ();
    g_autoptr (GtkBitset) mask = gtk_bitset_new_range (0, n);

    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), i);
        g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (row);
        if (!nautilus_view_item_is_group (item) && g_hash_table_contains (selected, item)) gtk_bitset_add (bits, i);
    }

    gtk_selection_model_set_selection (GTK_SELECTION_MODEL (self), bits, mask);
}

static void
on_group_expanded_changed (GtkTreeListRow  *row,
                           GParamSpec      *pspec,
                           NautilusViewModel *self)
{
    if (self->rebuilding_groups) return;

    g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (row);
    if (item == NULL || !nautilus_view_item_is_group (item)) return;

    const char *key = nautilus_view_item_get_group_key (item);
    gboolean changed;

    if (gtk_tree_list_row_get_expanded (row))
        changed = g_hash_table_remove (self->collapsed_groups, key);
    else
        changed = g_hash_table_add (self->collapsed_groups, g_strdup (key));

    (void) changed;
}

/*
 * The notify::expanded signal is useful for normal interaction, but source-model
 * updates may cause GtkTreeListModel to update rows while the grouping model is
 * being rebuilt.  Read the actual root-row state before every rebuild so the
 * hash table cannot lag behind GTK.
 */
static void
sync_collapsed_groups_from_rows (NautilusViewModel *self)
{
    if (!self->group_by_type || self->group_tree == NULL || self->group_roots == NULL)
        return;

    guint n = g_list_model_get_n_items (G_LIST_MODEL (self->group_roots));
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GtkTreeListRow) row = gtk_tree_list_model_get_child_row (self->group_tree, i);
        g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;

        if (item == NULL || !nautilus_view_item_is_group (item))
            continue;

        const char *key = nautilus_view_item_get_group_key (item);
        if (gtk_tree_list_row_get_expanded (row))
            g_hash_table_remove (self->collapsed_groups, key);
        else if (!g_hash_table_contains (self->collapsed_groups, key))
            g_hash_table_add (self->collapsed_groups, g_strdup (key));
    }
}

static void
apply_collapsed_groups_to_rows (NautilusViewModel *self)
{
    if (self->group_tree == NULL || self->group_roots == NULL)
        return;

    guint n = g_list_model_get_n_items (G_LIST_MODEL (self->group_roots));
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GtkTreeListRow) row = gtk_tree_list_model_get_child_row (self->group_tree, i);
        g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;

        if (item == NULL || !nautilus_view_item_is_group (item))
            continue;

        const char *key = nautilus_view_item_get_group_key (item);
        g_signal_handlers_disconnect_by_func (row, on_group_expanded_changed, self);
        gtk_tree_list_row_set_expanded (row, !g_hash_table_contains (self->collapsed_groups, key));
        g_signal_connect (row, "notify::expanded", G_CALLBACK (on_group_expanded_changed), self);
    }
}

static void
rebuild_type_groups (NautilusViewModel *self)
{
    if (!self->group_by_type || self->rebuilding_groups) return;

    sync_collapsed_groups_from_rows (self);
    self->rebuilding_groups = TRUE;
    g_autoptr (GHashTable) selected = remember_selected_items (self);
    g_autoptr (GHashTable) buckets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
    g_autoptr (GPtrArray) roots = g_ptr_array_new ();

    guint n = g_list_model_get_n_items (G_LIST_MODEL (self->sort_model));
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self->sort_model), i);
        NautilusViewItem *item = gtk_tree_list_row_get_item (row);
        g_autofree char *key = file_group_key (nautilus_view_item_get_file (item));
        GPtrArray *bucket = g_hash_table_lookup (buckets, key);

        if (bucket == NULL)
        {
            bucket = g_ptr_array_new_with_free_func (g_object_unref);
            g_hash_table_insert (buckets, g_strdup (key), bucket);
        }
        g_ptr_array_add (bucket, item);
    }

    g_autoptr (GList) keys = g_hash_table_get_keys (buckets);
    keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);

    for (GList *l = keys; l != NULL; l = l->next)
    {
        const char *key = l->data;
        GPtrArray *bucket = g_hash_table_lookup (buckets, key);
        NautilusViewItem *group = g_hash_table_lookup (self->groups, key);

        if (group == NULL)
        {
            group = nautilus_view_item_new_group (key);
            g_hash_table_insert (self->groups, g_strdup (key), group);
        }

        g_autofree char *description = g_str_equal (key, "0:directories") ? g_strdup ("Dossiers") : g_content_type_get_description (key);
        g_autofree char *title = g_strdup_printf ("%s (%u)", description ? description : key, bucket->len);
        nautilus_view_item_set_group_title (group, title);
        update_store (nautilus_view_item_get_group_children (group), bucket);
        g_ptr_array_add (roots, group);
    }

    update_store (self->group_roots, roots);

    apply_collapsed_groups_to_rows (self);

    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init (&iter, self->groups);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        if (!g_hash_table_contains (buckets, key)) g_hash_table_iter_remove (&iter);

    restore_selected_items (self, selected);
    self->rebuilding_groups = FALSE;
}

static void
on_source_items_changed (GListModel *source, guint position, guint removed, guint added, NautilusViewModel *self)
{
    if (self->group_by_type) rebuild_type_groups (self);
}

static void
on_source_sections_changed (GtkSectionModel *source, guint position, guint n_items, NautilusViewModel *self)
{
    /* Grouped presentation positions differ from source positions. */
    if (!self->group_by_type) gtk_section_model_sections_changed (GTK_SECTION_MODEL (self), position, n_items);
}

gboolean
nautilus_view_model_get_group_by_type (NautilusViewModel *self)
{
    return self->group_by_type;
}

void
nautilus_view_model_set_group_by_type (NautilusViewModel *self, gboolean enabled)
{
    if (self->group_by_type == enabled) return;

    if (self->group_by_type && !enabled)
        sync_collapsed_groups_from_rows (self);

    g_autoptr (GHashTable) selected = remember_selected_items (self);
    if (enabled)
    {
        nautilus_view_model_expand_as_a_tree (self, FALSE);
        self->group_roots = g_list_store_new (NAUTILUS_TYPE_VIEW_ITEM);
        self->groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
        self->group_tree = gtk_tree_list_model_new (G_LIST_MODEL (g_object_ref (self->group_roots)), FALSE, FALSE,
                                                    create_group_children, NULL, NULL);
    }

    self->group_by_type = enabled;
    GListModel *display = enabled ? G_LIST_MODEL (self->group_tree) : G_LIST_MODEL (self->sort_model);
    if (self->single_selection) gtk_single_selection_set_model (GTK_SINGLE_SELECTION (self->selection_model), display);
    else gtk_multi_selection_set_model (GTK_MULTI_SELECTION (self->selection_model), display);

    if (enabled)
        rebuild_type_groups (self);
    else
    {
        g_clear_object (&self->group_tree);
        g_clear_object (&self->group_roots);
        g_clear_pointer (&self->groups, g_hash_table_unref);
    }

    restore_selected_items (self, selected);
}

void
nautilus_view_model_set_collapsed_groups (NautilusViewModel *self,
                                          const char * const *keys)
{
    g_hash_table_remove_all (self->collapsed_groups);
    if (keys != NULL)
    {
        for (guint i = 0; keys[i] != NULL; i++)
            g_hash_table_add (self->collapsed_groups, g_strdup (keys[i]));
    }

    if (self->group_by_type)
        apply_collapsed_groups_to_rows (self);
}

static gint
compare_strv_elements (gconstpointer a,
                       gconstpointer b,
                       gpointer      user_data)
{
    const char * const *sa = a;
    const char * const *sb = b;
    return g_strcmp0 (*sa, *sb);
}

char **
nautilus_view_model_dup_collapsed_groups (NautilusViewModel *self)
{
    sync_collapsed_groups_from_rows (self);

    guint len = g_hash_table_size (self->collapsed_groups);
    char **result = g_new0 (char *, len + 1);
    GHashTableIter iter;
    gpointer key;
    guint i = 0;

    g_hash_table_iter_init (&iter, self->collapsed_groups);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        result[i++] = g_strdup (key);

    g_sort_array (result, len, sizeof (char *), compare_strv_elements, NULL);
    return result;
}

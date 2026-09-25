#include "nautilus-view-model.h"

#include "nautilus-directory.h"
#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-view-item.h"

/**
 * NautilusViewModel:
 *
 * Internal structure goes like this:
 *
 * selection_model : GtkSelectionModel<GtkTreeListRow<NautilusViewItem>>
 *  |
 *  +-- sort_model : GtkSectionModel<GtkTreeListRow<NautilusViewItem>>
 *       |
 *       +-- tree_model : GtkTreeListModel<GtkTreeListRow<NautilusViewItem>>
 *            |
 *            +-- root_filter_model : GtkFilterListModel<NautilusViewItem>
 *            |    |
 *            |    +-- GListStore<NautilusViewItem>
 *            |
 *         (0...n) GtkFilterListModel<NautilusViewItem>  //subdirectories
 *                 |
 *                 +-- GListStore<NautilusViewItem>
 *
 * The overall model item type is GtkTreeListRow, but the :filter and :sorter
 * properties are meant for internal models whose item type is NautilusViewItem.
 */

struct _NautilusViewModel
{
    GObject parent_instance;

    GHashTable *map_files_to_model;
    GHashTable *directory_reverse_map;

    GtkFilterListModel *root_filter_model;
    GtkTreeListModel *tree_model;
    GtkSortListModel *sort_model;
    GtkSelectionModel *selection_model;
    GListStore *group_roots;
    GtkTreeListModel *group_tree;
    GHashTable *groups;
    GHashTable *collapsed_groups;
    gboolean group_by_type;
    gboolean rebuilding_groups;

    gboolean single_selection;
    gboolean expand_as_a_tree;
    GList *cut_files;
};

static inline GListStore *
get_directory_store (NautilusViewModel *self,
                     NautilusFile      *directory)
{
    GListStore *store;

    store = g_hash_table_lookup (self->directory_reverse_map, directory);
    if (store == NULL)
    {
        store = G_LIST_STORE (gtk_filter_list_model_get_model (self->root_filter_model));
    }

    return store;
}

static GType
nautilus_view_model_get_item_type (GListModel *list)
{
    return GTK_TYPE_TREE_LIST_ROW;
}

static guint
nautilus_view_model_get_n_items (GListModel *list)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (list);

    if (self->selection_model == NULL)
    {
        return 0;
    }

    return g_list_model_get_n_items (G_LIST_MODEL (self->selection_model));
}

static gpointer
nautilus_view_model_get_item (GListModel *list,
                              guint       position)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (list);

    if (self->selection_model == NULL)
    {
        return NULL;
    }

    return g_list_model_get_item (G_LIST_MODEL (self->selection_model), position);
}

static void
nautilus_view_model_list_model_init (GListModelInterface *iface)
{
    iface->get_item_type = nautilus_view_model_get_item_type;
    iface->get_n_items = nautilus_view_model_get_n_items;
    iface->get_item = nautilus_view_model_get_item;
}

static void
nautilus_view_model_get_section (GtkSectionModel *model,
                                 guint            position,
                                 guint           *out_start,
                                 guint           *out_end)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    if (self->group_by_type)
    {
        guint n = g_list_model_get_n_items (G_LIST_MODEL (self));
        *out_start = position < n ? 0 : n;
        *out_end = position < n ? n : G_MAXUINT;
    }
    else
        gtk_section_model_get_section (GTK_SECTION_MODEL (self->sort_model), position, out_start, out_end);
}

static void
nautilus_view_model_section_model_init (GtkSectionModelInterface *iface)
{
    iface->get_section = nautilus_view_model_get_section;
}

static gboolean
nautilus_view_model_is_selected (GtkSelectionModel *model,
                                 guint              position)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    return gtk_selection_model_is_selected (self->selection_model, position);
}

static GtkBitset *
nautilus_view_model_get_selection_in_range (GtkSelectionModel *model,
                                            guint              pos,
                                            guint              n_items)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    return gtk_selection_model_get_selection_in_range (self->selection_model, pos, n_items);
}

static gboolean
nautilus_view_model_select_item (GtkSelectionModel *model,
                                 guint              position,
                                 gboolean           unselect_rest)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), position);
    g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;
    if (item == NULL || nautilus_view_item_is_group (item)) return FALSE;
    return gtk_selection_model_select_item (self->selection_model, position, unselect_rest);
}

static gboolean
nautilus_view_model_set_selection (GtkSelectionModel *model,
                                   GtkBitset         *selected,
                                   GtkBitset         *mask)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    g_autoptr (GtkBitset) safe = gtk_bitset_copy (selected);
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
    return gtk_selection_model_set_selection (self->selection_model, safe, mask);
}


static gboolean
nautilus_view_model_unselect_item (GtkSelectionModel *model,
                                   guint              position)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    return gtk_selection_model_unselect_item (self->selection_model, position);
}

static gboolean
nautilus_view_model_unselect_all (GtkSelectionModel *model)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (model);

    return gtk_selection_model_unselect_all (self->selection_model);
}


static void
nautilus_view_model_selection_model_init (GtkSelectionModelInterface *iface)
{
    iface->is_selected = nautilus_view_model_is_selected;
    iface->get_selection_in_range = nautilus_view_model_get_selection_in_range;
    iface->select_item = nautilus_view_model_select_item;
    iface->set_selection = nautilus_view_model_set_selection;
    iface->unselect_item = nautilus_view_model_unselect_item;
    iface->unselect_all = nautilus_view_model_unselect_all;
}

G_DEFINE_TYPE_WITH_CODE (NautilusViewModel, nautilus_view_model, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
                                                nautilus_view_model_list_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_SECTION_MODEL,
                                                nautilus_view_model_section_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_SELECTION_MODEL,
                                                nautilus_view_model_selection_model_init))

enum
{
    PROP_0,
    PROP_FILTER,
    PROP_SINGLE_SELECTION,
    PROP_SORTER,
    N_PROPS
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static void rebuild_type_groups (NautilusViewModel *self);
static void on_source_items_changed (GListModel *source, guint position, guint removed, guint added, NautilusViewModel *self);
static void on_source_sections_changed (GtkSectionModel *source, guint position, guint n_items, NautilusViewModel *self);

static void
dispose (GObject *object)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (object);

    if (self->selection_model != NULL)
    {
        g_signal_handlers_disconnect_by_func (self->selection_model,
                                              gtk_selection_model_selection_changed,
                                              self);
        g_signal_handlers_disconnect_by_func (self->selection_model, g_list_model_items_changed, self);
        g_object_unref (self->selection_model);
        self->selection_model = NULL;
    }

    if (self->sort_model != NULL)
    {
        g_signal_handlers_disconnect_by_func (self->sort_model,
                                              g_list_model_items_changed,
                                              self);
        g_signal_handlers_disconnect_by_func (self->sort_model,
                                              gtk_section_model_sections_changed,
                                              self);
        g_signal_handlers_disconnect_by_func (self->sort_model, on_source_items_changed, self);
        g_signal_handlers_disconnect_by_func (self->sort_model, on_source_sections_changed, self);
        g_object_unref (self->sort_model);
        self->sort_model = NULL;
    }

    g_clear_object (&self->group_tree);
    g_clear_object (&self->group_roots);
    g_clear_pointer (&self->groups, g_hash_table_unref);
    g_clear_pointer (&self->collapsed_groups, g_hash_table_unref);
    g_clear_object (&self->tree_model);
    g_clear_object (&self->root_filter_model);

    G_OBJECT_CLASS (nautilus_view_model_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (object);

    G_OBJECT_CLASS (nautilus_view_model_parent_class)->finalize (object);

    g_hash_table_destroy (self->map_files_to_model);
    g_hash_table_destroy (self->directory_reverse_map);

    g_clear_list (&self->cut_files, g_object_unref);
}

static void
get_property (GObject    *object,
              guint       prop_id,
              GValue     *value,
              GParamSpec *pspec)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (object);

    switch (prop_id)
    {
        case PROP_FILTER:
        {
            g_value_set_object (value, nautilus_view_model_get_filter (self));
        }
        break;

        case PROP_SINGLE_SELECTION:
        {
            g_value_set_boolean (value, nautilus_view_model_get_single_selection (self));
        }
        break;

        case PROP_SORTER:
        {
            g_value_set_object (value, nautilus_view_model_get_sorter (self));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static void
set_property (GObject      *object,
              guint         prop_id,
              const GValue *value,
              GParamSpec   *pspec)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (object);

    switch (prop_id)
    {
        case PROP_FILTER:
        {
            nautilus_view_model_set_filter (self, g_value_get_object (value));
        }
        break;

        case PROP_SINGLE_SELECTION:
        {
            self->single_selection = g_value_get_boolean (value);
        }
        break;

        case PROP_SORTER:
        {
            nautilus_view_model_set_sorter (self, g_value_get_object (value));
        }
        break;

        default:
        {
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
    }
}

static GListModel *
create_model_func (GObject           *item,
                   NautilusViewModel *self)
{
    NautilusFile *file;
    GListStore *store;

    file = nautilus_view_item_get_file (NAUTILUS_VIEW_ITEM (item));
    if (!nautilus_file_is_directory (file))
    {
        return NULL;
    }

    store = g_hash_table_lookup (self->directory_reverse_map, file);
    if (store == NULL)
    {
        store = g_list_store_new (NAUTILUS_TYPE_VIEW_ITEM);
        g_hash_table_insert (self->directory_reverse_map, file, store);
    }

    GtkFilterListModel *filter_model = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (store)), NULL);

    g_object_bind_property (self->root_filter_model, "filter",
                            filter_model, "filter",
                            G_BINDING_SYNC_CREATE);

    return G_LIST_MODEL (filter_model);
}

static void
constructed (GObject *object)
{
    NautilusViewModel *self = NAUTILUS_VIEW_MODEL (object);

    G_OBJECT_CLASS (nautilus_view_model_parent_class)->constructed (object);

    self->root_filter_model = gtk_filter_list_model_new (G_LIST_MODEL (g_list_store_new (NAUTILUS_TYPE_VIEW_ITEM)), NULL);

    self->tree_model = gtk_tree_list_model_new (g_object_ref (G_LIST_MODEL (self->root_filter_model)),
                                                FALSE, FALSE,
                                                (GtkTreeListModelCreateModelFunc) create_model_func,
                                                self, NULL);
    self->sort_model = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (self->tree_model)), NULL);

    if (self->single_selection)
    {
        GtkSingleSelection *single = gtk_single_selection_new (NULL);

        gtk_single_selection_set_autoselect (single, FALSE);
        gtk_single_selection_set_can_unselect (single, TRUE);

        gtk_single_selection_set_model (single, G_LIST_MODEL (self->sort_model));
        self->selection_model = GTK_SELECTION_MODEL (single);
    }
    else
    {
        self->selection_model = GTK_SELECTION_MODEL (gtk_multi_selection_new (g_object_ref (G_LIST_MODEL (self->sort_model))));
    }

    self->map_files_to_model = g_hash_table_new (NULL, NULL);
    self->directory_reverse_map = g_hash_table_new_full (NULL, NULL, NULL, g_object_unref);

    g_signal_connect (self->sort_model, "items-changed", G_CALLBACK (on_source_items_changed), self);
    g_signal_connect_swapped (self->selection_model, "items-changed", G_CALLBACK (g_list_model_items_changed), self);
    g_signal_connect (self->sort_model, "sections-changed", G_CALLBACK (on_source_sections_changed), self);
    g_signal_connect_swapped (self->selection_model, "selection-changed",
                              G_CALLBACK (gtk_selection_model_selection_changed), self);
}

static void
nautilus_view_model_class_init (NautilusViewModelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = dispose;
    object_class->finalize = finalize;
    object_class->get_property = get_property;
    object_class->set_property = set_property;
    object_class->constructed = constructed;

    properties[PROP_FILTER] =
        g_param_spec_object ("filter", NULL, NULL,
                             GTK_TYPE_FILTER,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
    properties[PROP_SINGLE_SELECTION] =
        g_param_spec_boolean ("single-selection", NULL, NULL,
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
    properties[PROP_SORTER] =
        g_param_spec_object ("sorter", NULL, NULL,
                             GTK_TYPE_SORTER,
                             G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
nautilus_view_model_init (NautilusViewModel *self)
{
    self->collapsed_groups = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static gint
compare_data_func (gconstpointer a,
                   gconstpointer b,
                   gpointer      user_data)
{
    NautilusViewModel *self = (NautilusViewModel *) user_data;

    if (nautilus_view_model_get_sorter (self) == NULL)
    {
        return GTK_ORDERING_EQUAL;
    }

    return gtk_sorter_compare (nautilus_view_model_get_sorter (self), (gpointer) a, (gpointer) b);
}

NautilusViewModel *
nautilus_view_model_new (gboolean single_selection)
{
    return g_object_new (NAUTILUS_TYPE_VIEW_MODEL,
                         "single-selection", single_selection,
                         NULL);
}

GtkFilter *
nautilus_view_model_get_filter (NautilusViewModel *self)
{
    return gtk_filter_list_model_get_filter (self->root_filter_model);
}

void
nautilus_view_model_set_filter (NautilusViewModel *self,
                                GtkFilter         *filter)
{
    if (self->root_filter_model == NULL ||
        gtk_filter_list_model_get_filter (self->root_filter_model) == filter)
    {
        return;
    }

    gtk_filter_list_model_set_filter (self->root_filter_model, filter);
    /* Subdirectory filter models are synchronized through bindings. */

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILTER]);
}

gboolean
nautilus_view_model_get_single_selection (NautilusViewModel *self)
{
    return self->single_selection;
}

GtkSorter *
nautilus_view_model_get_sorter (NautilusViewModel *self)
{
    GtkTreeListRowSorter *row_sorter;

    row_sorter = (GtkTreeListRowSorter *) gtk_sort_list_model_get_sorter (self->sort_model);

    return row_sorter != NULL ? gtk_tree_list_row_sorter_get_sorter (row_sorter) : NULL;
}

void
nautilus_view_model_set_sorter (NautilusViewModel *self,
                                GtkSorter         *sorter)
{
    g_autoptr (GtkTreeListRowSorter) row_sorter = NULL;

    row_sorter = gtk_tree_list_row_sorter_new (NULL);

    gtk_tree_list_row_sorter_set_sorter (row_sorter, sorter);
    gtk_sort_list_model_set_sorter (self->sort_model, GTK_SORTER (row_sorter));

    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_SORTER]);
}

/**
 * Set the section sorter, effectively enabling sections.
 *
 * Unlike the regular sorter, which compares NautilusViewItem objects, this one
 * compares the GtkTreeListRows objects which wrap the NautilusViewItem objects.
 */
void
nautilus_view_model_set_section_sorter (NautilusViewModel *self,
                                        GtkSorter         *section_sorter)
{
    gtk_sort_list_model_set_section_sorter (self->sort_model, GTK_SORTER (section_sorter));
}

void
nautilus_view_model_sort (NautilusViewModel *self)
{
    GtkSorter *sorter = nautilus_view_model_get_sorter (self);

    if (sorter != NULL)
    {
        gtk_sorter_changed (sorter, GTK_SORTER_CHANGE_DIFFERENT);
    }
    if (self->group_by_type) rebuild_type_groups (self);
}

GList *
nautilus_view_model_get_sorted_items_for_files (NautilusViewModel *self,
                                                GList             *files)
{
    GList *items = NULL;

    for (GList *l = files; l != NULL; l = l->next)
    {
        NautilusViewItem *item;

        item = nautilus_view_model_get_item_for_file (self, l->data);
        if (item != NULL)
        {
            items = g_list_prepend (items, item);
        }
    }

    return g_list_sort_with_data (g_list_copy (items), compare_data_func, self);
}

NautilusViewItem *
nautilus_view_model_get_item_for_file (NautilusViewModel *self,
                                       NautilusFile      *file)
{
    return g_hash_table_lookup (self->map_files_to_model, file);
}

void
nautilus_view_model_remove_items (NautilusViewModel *self,
                                  GHashTable        *items,
                                  NautilusDirectory *directory)
{
    g_autoptr (NautilusFile) parent = nautilus_directory_get_corresponding_file (directory);
    GListStore *dir_store = get_directory_store (self, parent);
    guint range_start, n_items_in_range = 0;

    /* Remove contiguous item ranges to minimize ::items-changed emissions.
     * Remove after passing the range to not impact the index. */
    for (gint i = g_list_model_get_n_items (G_LIST_MODEL (dir_store)) - 1;
         i >= 0 && g_hash_table_size (items) > 0; i--)
    {
        g_autoptr (NautilusViewItem) item = g_list_model_get_item (G_LIST_MODEL (dir_store), i);
        NautilusFile *file = NULL;

        if (!g_hash_table_steal_extended (items, item, NULL, (gpointer *) &file))
        {
            if (n_items_in_range > 0)
            {
                g_list_store_splice (dir_store, range_start, n_items_in_range, NULL, 0);
                n_items_in_range = 0;
            }

            continue;
        }

        g_hash_table_remove (self->map_files_to_model, file);
        if (nautilus_file_is_directory (file))
        {
            g_hash_table_remove (self->directory_reverse_map, file);
        }

        /* The previous item is contiguous, keep growing the range. */
        n_items_in_range++;
        range_start = i;
    }

    if (n_items_in_range > 0)
    {
        /* Flush the leftover range. */
        g_list_store_splice (dir_store, range_start, n_items_in_range, NULL, 0);
    }

    if (g_hash_table_size (items) > 0)
    {
        g_warning ("Failed to remove %u item(s)", g_hash_table_size (items));
    }
}

void
nautilus_view_model_remove_all_items (NautilusViewModel *self)
{
    g_list_store_remove_all (G_LIST_STORE (gtk_filter_list_model_get_model (self->root_filter_model)));
    g_hash_table_remove_all (self->map_files_to_model);
    g_hash_table_remove_all (self->directory_reverse_map);
}

void
nautilus_view_model_add_item (NautilusViewModel *self,
                              NautilusViewItem  *item)
{
    NautilusFile *file;
    g_autoptr (NautilusFile) parent = NULL;

    file = nautilus_view_item_get_file (item);
    parent = nautilus_file_get_parent (file);

    g_list_store_append (get_directory_store (self, parent), item);
    g_hash_table_insert (self->map_files_to_model, file, item);
}

static void
splice_items_into_common_parent (NautilusViewModel *self,
                                 GPtrArray         *items,
                                 NautilusFile      *common_parent)
{
    GListStore *dir_store;

    dir_store = get_directory_store (self, common_parent);
    g_list_store_splice (dir_store,
                         g_list_model_get_n_items (G_LIST_MODEL (dir_store)),
                         0, items->pdata, items->len);
}

void
nautilus_view_model_add_items (NautilusViewModel *self,
                               GList             *items)
{
    g_autoptr (GPtrArray) array = g_ptr_array_new ();
    g_autoptr (NautilusFile) previous_parent = NULL;
    g_autoptr (GList) sorted_items = NULL;
    NautilusViewItem *item;

    /* The first added file becomes the initial focus and scroll anchor, so we
     * need to sort items before adding them to the internal model. */
    sorted_items = g_list_sort_with_data (g_list_copy (items), compare_data_func, self);

    for (GList *l = sorted_items; l != NULL; l = l->next)
    {
        g_autoptr (NautilusFile) parent = NULL;

        item = NAUTILUS_VIEW_ITEM (l->data);
        parent = nautilus_file_get_parent (nautilus_view_item_get_file (item));

        if (previous_parent != NULL && previous_parent != parent)
        {
            /* The pending items share a common parent. */
            splice_items_into_common_parent (self, array, previous_parent);

            /* Clear pending items and start a new with a new parent. */
            g_ptr_array_unref (array);
            array = g_ptr_array_new ();
        }
        g_set_object (&previous_parent, parent);

        g_ptr_array_add (array, item);
        g_hash_table_insert (self->map_files_to_model,
                             nautilus_view_item_get_file (item),
                             item);
    }

    if (previous_parent != NULL)
    {
        /* Flush the pending items. */
        splice_items_into_common_parent (self, array, previous_parent);
    }
}

void
nautilus_view_model_clear_subdirectory (NautilusViewModel *self,
                                        NautilusViewItem  *item)
{
    NautilusFile *file;
    GListModel *children;
    guint n_children = 0;

    g_return_if_fail (NAUTILUS_IS_VIEW_MODEL (self));
    g_return_if_fail (NAUTILUS_IS_VIEW_ITEM (item));

    file = nautilus_view_item_get_file (item);
    children = G_LIST_MODEL (g_hash_table_lookup (self->directory_reverse_map, file));
    n_children = (children != NULL) ? g_list_model_get_n_items (children) : 0;
    for (guint i = 0; i < n_children; i++)
    {
        g_autoptr (NautilusViewItem) child = g_list_model_get_item (children, i);

        if (nautilus_file_is_directory (nautilus_view_item_get_file (child)))
        {
            /* Clear recursively */
            nautilus_view_model_clear_subdirectory (self, child);
        }
    }
    g_hash_table_remove (self->directory_reverse_map, file);
}

static inline void
collapse_all_rows (NautilusViewModel *self)
{
    guint n_root_items = g_list_model_get_n_items (gtk_tree_list_model_get_model (self->tree_model));

    for (guint i = 0; i < n_root_items; i++)
    {
        g_autoptr (GtkTreeListRow) root_level_row = gtk_tree_list_model_get_child_row (self->tree_model, i);

        gtk_tree_list_row_set_expanded (root_level_row, FALSE);
    }
}

void
nautilus_view_model_expand_as_a_tree (NautilusViewModel *self,
                                      gboolean           expand_as_a_tree)
{
    if (self->expand_as_a_tree && !expand_as_a_tree)
    {
        collapse_all_rows (self);
    }

    self->expand_as_a_tree = expand_as_a_tree;
}

void
nautilus_view_model_set_cut_files (NautilusViewModel *self,
                                   GList             *cut_files)
{
    NautilusViewItem *item;

    for (GList *l = self->cut_files; l != NULL; l = l->next)
    {
        item = nautilus_view_model_get_item_for_file (self, l->data);
        if (item != NULL)
        {
            nautilus_view_item_set_cut (item, FALSE);
        }
    }
    g_clear_list (&self->cut_files, g_object_unref);

    for (GList *l = cut_files; l != NULL; l = l->next)
    {
        item = nautilus_view_model_get_item_for_file (self, l->data);
        if (item != NULL)
        {
            self->cut_files = g_list_prepend (self->cut_files,
                                              g_object_ref (l->data));
            nautilus_view_item_set_cut (item, TRUE);
        }
    }
}
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


GListModel *
nautilus_view_model_dup_group_roots_model (NautilusViewModel *self)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_MODEL (self), NULL);

    return self->group_roots != NULL ? G_LIST_MODEL (g_object_ref (self->group_roots)) : NULL;
}

gboolean
nautilus_view_model_get_group_collapsed (NautilusViewModel *self,
                                         const char        *key)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_MODEL (self), FALSE);
    g_return_val_if_fail (key != NULL, FALSE);

    return g_hash_table_contains (self->collapsed_groups, key);
}

void
nautilus_view_model_set_group_collapsed (NautilusViewModel *self,
                                         const char        *key,
                                         gboolean           collapsed)
{
    g_return_if_fail (NAUTILUS_IS_VIEW_MODEL (self));
    g_return_if_fail (key != NULL);

    if (collapsed)
    {
        if (!g_hash_table_contains (self->collapsed_groups, key))
            g_hash_table_add (self->collapsed_groups, g_strdup (key));
    }
    else
    {
        g_hash_table_remove (self->collapsed_groups, key);
    }

    if (self->group_tree == NULL || self->group_roots == NULL)
        return;

    guint n = g_list_model_get_n_items (G_LIST_MODEL (self->group_roots));
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (NautilusViewItem) group = g_list_model_get_item (G_LIST_MODEL (self->group_roots), i);
        if (group == NULL || g_strcmp0 (nautilus_view_item_get_group_key (group), key) != 0)
            continue;

        g_autoptr (GtkTreeListRow) row = gtk_tree_list_model_get_child_row (self->group_tree, i);
        if (row != NULL)
        {
            g_signal_handlers_disconnect_by_func (row, on_group_expanded_changed, self);
            gtk_tree_list_row_set_expanded (row, !collapsed);
            g_signal_connect (row, "notify::expanded", G_CALLBACK (on_group_expanded_changed), self);
        }
        break;
    }
}

guint
nautilus_view_model_get_position_for_item (NautilusViewModel *self,
                                           NautilusViewItem  *item)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_MODEL (self), GTK_INVALID_LIST_POSITION);
    g_return_val_if_fail (NAUTILUS_IS_VIEW_ITEM (item), GTK_INVALID_LIST_POSITION);

    guint n = g_list_model_get_n_items (G_LIST_MODEL (self));
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (self), i);
        g_autoptr (NautilusViewItem) candidate = row ? gtk_tree_list_row_get_item (row) : NULL;
        if (candidate == item)
            return i;
    }

    return GTK_INVALID_LIST_POSITION;
}

gboolean
nautilus_view_model_find_group_item (NautilusViewModel *self,
                                     NautilusViewItem  *item,
                                     guint             *out_group_position,
                                     guint             *out_child_position)
{
    g_return_val_if_fail (NAUTILUS_IS_VIEW_MODEL (self), FALSE);
    g_return_val_if_fail (NAUTILUS_IS_VIEW_ITEM (item), FALSE);

    if (self->group_roots == NULL)
        return FALSE;

    guint group_n = g_list_model_get_n_items (G_LIST_MODEL (self->group_roots));
    for (guint group_pos = 0; group_pos < group_n; group_pos++)
    {
        g_autoptr (NautilusViewItem) group = g_list_model_get_item (G_LIST_MODEL (self->group_roots), group_pos);
        GListStore *children = group ? nautilus_view_item_get_group_children (group) : NULL;
        if (children == NULL)
            continue;

        guint child_n = g_list_model_get_n_items (G_LIST_MODEL (children));
        for (guint child_pos = 0; child_pos < child_n; child_pos++)
        {
            g_autoptr (NautilusViewItem) candidate = g_list_model_get_item (G_LIST_MODEL (children), child_pos);
            if (candidate == item)
            {
                if (out_group_position != NULL)
                    *out_group_position = group_pos;
                if (out_child_position != NULL)
                    *out_child_position = child_pos;
                return TRUE;
            }
        }
    }

    return FALSE;
}

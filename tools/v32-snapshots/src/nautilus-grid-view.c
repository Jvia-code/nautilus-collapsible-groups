/*
 * Copyright (C) 2022 The GNOME project contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "nautilus-grid-view.h"

#include "nautilus-file.h"
#include "nautilus-global-preferences.h"
#include "nautilus-grid-cell.h"
#include "nautilus-list-base-private.h"
#include "nautilus-view-cell.h"
#include "nautilus-view-item.h"
#include "nautilus-view-model.h"

#include <glib/gi18n.h>

struct _NautilusGridView
{
    NautilusListBase parent_instance;

    GtkGridView *view_ui;
    GtkBox *group_view_ui;
    GListModel *group_roots_model;

    gint zoom_level;

    gboolean directories_first;

    GQuark caption_attributes[NAUTILUS_GRID_CELL_N_CAPTIONS];

    GQuark sort_attribute;
    gboolean reversed;
    gboolean group_by_type;
    gboolean rubberband_enabled;
};

G_DEFINE_TYPE (NautilusGridView, nautilus_grid_view, NAUTILUS_TYPE_LIST_BASE)

#define get_view_item(li) \
        (NAUTILUS_VIEW_ITEM (gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (gtk_list_item_get_item (li)))))

/* A small selection-model proxy used by each per-group GtkGridView.  The
 * nested grid exposes NautilusViewItem directly, while selection remains owned
 * by the single NautilusViewModel for the whole directory.  GTK explicitly
 * supports sharing selection state between different selection models/views. */
typedef struct _NautilusGroupSelectionModel
{
    GObject parent_instance;
    GListModel *items;
    NautilusViewModel *parent_model;
} NautilusGroupSelectionModel;

typedef struct _NautilusGroupSelectionModelClass
{
    GObjectClass parent_class;
} NautilusGroupSelectionModelClass;

/* G_DEFINE_TYPE_WITH_CODE() emits the get_type() definition. Nautilus builds
 * with -Werror=missing-prototypes, so give this private type a prototype first. */
static GType nautilus_group_selection_model_get_type (void);

#define NAUTILUS_TYPE_GROUP_SELECTION_MODEL (nautilus_group_selection_model_get_type ())
#define NAUTILUS_GROUP_SELECTION_MODEL(obj) ((NautilusGroupSelectionModel *) (obj))

static void group_selection_list_model_init (GListModelInterface *iface);
static void group_selection_selection_model_init (GtkSelectionModelInterface *iface);

G_DEFINE_TYPE_WITH_CODE (NautilusGroupSelectionModel,
                         nautilus_group_selection_model,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL,
                                                group_selection_list_model_init)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_SELECTION_MODEL,
                                                group_selection_selection_model_init))

static GType
group_selection_get_item_type (GListModel *list)
{
    return NAUTILUS_TYPE_VIEW_ITEM;
}

static guint
group_selection_get_n_items (GListModel *list)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (list);
    return self->items != NULL ? g_list_model_get_n_items (self->items) : 0;
}

static gpointer
group_selection_get_item (GListModel *list,
                          guint       position)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (list);
    return self->items != NULL ? g_list_model_get_item (self->items, position) : NULL;
}

static guint
group_selection_global_position (NautilusGroupSelectionModel *self,
                                 guint                        local_position)
{
    if (self->items == NULL || self->parent_model == NULL)
        return GTK_INVALID_LIST_POSITION;

    g_autoptr (NautilusViewItem) item = g_list_model_get_item (self->items, local_position);
    if (item == NULL)
        return GTK_INVALID_LIST_POSITION;

    return nautilus_view_model_get_position_for_item (self->parent_model, item);
}

static gboolean
group_selection_is_selected (GtkSelectionModel *selection,
                             guint              position)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    guint global_position = group_selection_global_position (self, position);

    return global_position != GTK_INVALID_LIST_POSITION &&
           gtk_selection_model_is_selected (GTK_SELECTION_MODEL (self->parent_model), global_position);
}

static GtkBitset *
group_selection_get_selection_in_range (GtkSelectionModel *selection,
                                        guint              position,
                                        guint              n_items)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    g_autoptr (GtkBitset) result = gtk_bitset_new_empty ();
    guint end = MIN (position + n_items, group_selection_get_n_items (G_LIST_MODEL (self)));

    for (guint i = position; i < end; i++)
    {
        if (group_selection_is_selected (selection, i))
            gtk_bitset_add (result, i);
    }

    return g_steal_pointer (&result);
}

static gboolean
group_selection_select_item (GtkSelectionModel *selection,
                             guint              position,
                             gboolean           unselect_rest)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    guint global_position = group_selection_global_position (self, position);

    if (global_position == GTK_INVALID_LIST_POSITION)
        return FALSE;

    return gtk_selection_model_select_item (GTK_SELECTION_MODEL (self->parent_model),
                                            global_position,
                                            unselect_rest);
}

static gboolean
group_selection_unselect_item (GtkSelectionModel *selection,
                               guint              position)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    guint global_position = group_selection_global_position (self, position);

    if (global_position == GTK_INVALID_LIST_POSITION)
        return FALSE;

    return gtk_selection_model_unselect_item (GTK_SELECTION_MODEL (self->parent_model), global_position);
}

static gboolean
group_selection_select_range (GtkSelectionModel *selection,
                              guint              position,
                              guint              n_items,
                              gboolean           unselect_rest)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    guint end = MIN (position + n_items, group_selection_get_n_items (G_LIST_MODEL (self)));
    gboolean success = TRUE;

    if (unselect_rest)
        success &= gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (self->parent_model));

    for (guint i = position; i < end; i++)
    {
        guint global_position = group_selection_global_position (self, i);
        if (global_position != GTK_INVALID_LIST_POSITION)
            success &= gtk_selection_model_select_item (GTK_SELECTION_MODEL (self->parent_model),
                                                        global_position,
                                                        FALSE);
    }

    return success;
}

static gboolean
group_selection_unselect_range (GtkSelectionModel *selection,
                                guint              position,
                                guint              n_items)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    guint end = MIN (position + n_items, group_selection_get_n_items (G_LIST_MODEL (self)));
    gboolean success = TRUE;

    for (guint i = position; i < end; i++)
    {
        guint global_position = group_selection_global_position (self, i);
        if (global_position != GTK_INVALID_LIST_POSITION)
            success &= gtk_selection_model_unselect_item (GTK_SELECTION_MODEL (self->parent_model),
                                                          global_position);
    }

    return success;
}

static gboolean
group_selection_select_all (GtkSelectionModel *selection)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    return gtk_selection_model_select_all (GTK_SELECTION_MODEL (self->parent_model));
}

static gboolean
group_selection_unselect_all (GtkSelectionModel *selection)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    return gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (self->parent_model));
}

static gboolean
group_selection_set_selection (GtkSelectionModel *selection,
                               GtkBitset         *selected,
                               GtkBitset         *mask)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (selection);
    g_autoptr (GtkBitset) global_selected = gtk_bitset_new_empty ();
    g_autoptr (GtkBitset) global_mask = gtk_bitset_new_empty ();
    GtkBitsetIter iter;
    guint local_position;

    for (gtk_bitset_iter_init_first (&iter, mask, &local_position);
         gtk_bitset_iter_is_valid (&iter);
         gtk_bitset_iter_next (&iter, &local_position))
    {
        guint global_position = group_selection_global_position (self, local_position);
        if (global_position == GTK_INVALID_LIST_POSITION)
            continue;

        gtk_bitset_add (global_mask, global_position);
        if (gtk_bitset_contains (selected, local_position))
            gtk_bitset_add (global_selected, global_position);
    }

    return gtk_selection_model_set_selection (GTK_SELECTION_MODEL (self->parent_model),
                                              global_selected,
                                              global_mask);
}

static void
group_selection_list_model_init (GListModelInterface *iface)
{
    iface->get_item_type = group_selection_get_item_type;
    iface->get_n_items = group_selection_get_n_items;
    iface->get_item = group_selection_get_item;
}

static void
group_selection_selection_model_init (GtkSelectionModelInterface *iface)
{
    iface->is_selected = group_selection_is_selected;
    iface->get_selection_in_range = group_selection_get_selection_in_range;
    iface->select_item = group_selection_select_item;
    iface->unselect_item = group_selection_unselect_item;
    iface->select_range = group_selection_select_range;
    iface->unselect_range = group_selection_unselect_range;
    iface->select_all = group_selection_select_all;
    iface->unselect_all = group_selection_unselect_all;
    iface->set_selection = group_selection_set_selection;
}

static void
group_selection_items_changed (GListModel                   *items,
                               guint                         position,
                               guint                         removed,
                               guint                         added,
                               NautilusGroupSelectionModel *self)
{
    g_list_model_items_changed (G_LIST_MODEL (self), position, removed, added);
    if (group_selection_get_n_items (G_LIST_MODEL (self)) > 0)
        gtk_selection_model_selection_changed (GTK_SELECTION_MODEL (self),
                                               0,
                                               group_selection_get_n_items (G_LIST_MODEL (self)));
}

static void
group_selection_parent_changed (GtkSelectionModel           *parent,
                                guint                        position,
                                guint                        n_items,
                                NautilusGroupSelectionModel *self)
{
    guint n = group_selection_get_n_items (G_LIST_MODEL (self));
    if (n > 0)
        gtk_selection_model_selection_changed (GTK_SELECTION_MODEL (self), 0, n);
}

static void
group_selection_parent_items_changed (GListModel                   *parent,
                                      guint                         position,
                                      guint                         removed,
                                      guint                         added,
                                      NautilusGroupSelectionModel *self)
{
    /* Global positions shift when another group is expanded/collapsed or when
     * the directory changes. Rebind every visible local cell so its cached
     * NautilusViewCell:position remains a valid global model position. */
    guint n = group_selection_get_n_items (G_LIST_MODEL (self));
    if (n > 0)
        g_list_model_items_changed (G_LIST_MODEL (self), 0, n, n);
}

static void
nautilus_group_selection_model_dispose (GObject *object)
{
    NautilusGroupSelectionModel *self = NAUTILUS_GROUP_SELECTION_MODEL (object);

    g_clear_object (&self->items);
    g_clear_object (&self->parent_model);

    G_OBJECT_CLASS (nautilus_group_selection_model_parent_class)->dispose (object);
}

static void
nautilus_group_selection_model_class_init (NautilusGroupSelectionModelClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = nautilus_group_selection_model_dispose;
}

static void
nautilus_group_selection_model_init (NautilusGroupSelectionModel *self)
{
}

static GtkSelectionModel *
nautilus_group_selection_model_new (NautilusViewModel *parent_model,
                                    GListModel        *items)
{
    NautilusGroupSelectionModel *self = g_object_new (NAUTILUS_TYPE_GROUP_SELECTION_MODEL, NULL);

    self->parent_model = g_object_ref (parent_model);
    self->items = g_object_ref (items);
    g_signal_connect_object (items,
                             "items-changed",
                             G_CALLBACK (group_selection_items_changed),
                             self,
                             0);
    g_signal_connect_object (parent_model,
                             "selection-changed",
                             G_CALLBACK (group_selection_parent_changed),
                             self,
                             0);
    g_signal_connect_object (parent_model,
                             "items-changed",
                             G_CALLBACK (group_selection_parent_items_changed),
                             self,
                             0);

    return GTK_SELECTION_MODEL (self);
}

static const NautilusViewInfo grid_view_info =
{
    .view_id = NAUTILUS_VIEW_GRID_ID,
    .zoom_level_min = NAUTILUS_GRID_ZOOM_LEVEL_SMALL,
    .zoom_level_max = NAUTILUS_GRID_ZOOM_LEVEL_EXTRA_LARGE,
    .zoom_level_standard = NAUTILUS_GRID_ZOOM_LEVEL_MEDIUM,
};

static NautilusViewInfo
real_get_view_info (NautilusListBase *list_base)
{
    return grid_view_info;
}

static gint
nautilus_grid_view_sort (gconstpointer a,
                         gconstpointer b,
                         gpointer      user_data)
{
    NautilusGridView *self = user_data;
    NautilusFile *file_a;
    NautilusFile *file_b;

    file_a = nautilus_view_item_get_file ((NautilusViewItem *) a);
    file_b = nautilus_view_item_get_file ((NautilusViewItem *) b);

    return nautilus_file_compare_for_sort_by_attribute_q (file_a, file_b,
                                                          self->sort_attribute,
                                                          self->directories_first,
                                                          self->reversed);
}

static void
update_sort_directories_first (NautilusGridView *self)
{
    NautilusFile *directory_as_file = nautilus_list_base_get_directory_as_file (NAUTILUS_LIST_BASE (self));
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));

    /* Always treat directories as normal items in search and recent. Recent
     * can accidentally contain directories when they were picked via file chooser. */
    if (nautilus_file_is_in_search (directory_as_file) ||
        nautilus_file_is_in_recent (directory_as_file))
    {
        self->directories_first = FALSE;
    }
    else
    {
        self->directories_first = g_settings_get_boolean (gtk_filechooser_preferences,
                                                          NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST);
    }

    if (model != NULL)
    {
        nautilus_view_model_sort (model);
    }
}

static void
nautilus_grid_view_setup_directory (NautilusListBase  *list_base,
                                    NautilusDirectory *new_directory)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);

    NAUTILUS_LIST_BASE_CLASS (nautilus_grid_view_parent_class)->setup_directory (list_base, new_directory);

    update_sort_directories_first (self);
}

static guint
get_icon_size_for_zoom_level (NautilusGridZoomLevel zoom_level)
{
    switch (zoom_level)
    {
        case NAUTILUS_GRID_ZOOM_LEVEL_SMALL:
        {
            return NAUTILUS_GRID_ICON_SIZE_SMALL;
        }
        break;

        case NAUTILUS_GRID_ZOOM_LEVEL_SMALL_PLUS:
        {
            return NAUTILUS_GRID_ICON_SIZE_SMALL_PLUS;
        }
        break;

        case NAUTILUS_GRID_ZOOM_LEVEL_MEDIUM:
        {
            return NAUTILUS_GRID_ICON_SIZE_MEDIUM;
        }
        break;

        case NAUTILUS_GRID_ZOOM_LEVEL_LARGE:
        {
            return NAUTILUS_GRID_ICON_SIZE_LARGE;
        }
        break;

        case NAUTILUS_GRID_ZOOM_LEVEL_EXTRA_LARGE:
        {
            return NAUTILUS_GRID_ICON_SIZE_EXTRA_LARGE;
        }
        break;
    }
    g_return_val_if_reached (NAUTILUS_GRID_ICON_SIZE_MEDIUM);
}

static gint
get_default_zoom_level (void)
{
    int default_zoom_level = g_settings_get_enum (nautilus_icon_view_preferences,
                                                  NAUTILUS_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL);

    /* Sanitize preference value */
    return CLAMP (default_zoom_level,
                  grid_view_info.zoom_level_min,
                  grid_view_info.zoom_level_max);
}

static void
set_captions_from_preferences (NautilusGridView *self)
{
    g_auto (GStrv) value = NULL;
    gint n_captions_for_zoom_level;

    value = g_settings_get_strv (nautilus_icon_view_preferences,
                                 NAUTILUS_PREFERENCES_ICON_VIEW_CAPTIONS);

    /* Set a celling on the number of captions depending on the zoom level. */
    n_captions_for_zoom_level = MIN ((uint) self->zoom_level + 1,
                                     G_N_ELEMENTS (self->caption_attributes));

    /* Reset array to zeros beforehand, as we may not refill all elements. */
    memset (&self->caption_attributes, 0, sizeof (self->caption_attributes));
    for (gint i = 0, quark_i = 0;
         value[i] != NULL && quark_i < n_captions_for_zoom_level;
         i++)
    {
        if (g_strcmp0 (value[i], "none") == 0)
        {
            continue;
        }

        /* Convert to quarks in advance, otherwise each NautilusFile attribute
         * getter would call g_quark_from_string() once for each file. */
        self->caption_attributes[quark_i] = g_quark_from_string (value[i]);
        quark_i++;
    }
}

static void
real_set_zoom_level (NautilusListBase *list_base,
                     int               new_level)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);

    g_return_if_fail (new_level >= grid_view_info.zoom_level_min &&
                      new_level <= grid_view_info.zoom_level_max);

    self->zoom_level = new_level;

    if (g_settings_get_enum (nautilus_icon_view_preferences,
                             NAUTILUS_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL) != new_level)
    {
        g_settings_set_enum (nautilus_icon_view_preferences,
                             NAUTILUS_PREFERENCES_ICON_VIEW_DEFAULT_ZOOM_LEVEL,
                             new_level);
    }

    /* The zoom level may change how many captions are allowed. Update it before
     * notifying the icon size change, under the assumption that NautilusGridCell
     * updates captions whenever the icon size is set*/
    set_captions_from_preferences (self);

    g_object_notify (G_OBJECT (self), "icon-size");
}

/* The generic implementation in src/nautilus-list-base.c doesn't allow the
 * 2-dimensional movements expected from a grid. Let's hack GTK here. */
static void
real_preview_selection_event (NautilusListBase *list_base,
                              GtkDirectionType  direction)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);
    guint direction_keyval;
    g_autoptr (GtkShortcutTrigger) direction_trigger = NULL;
    g_autoptr (GListModel) controllers = NULL;
    gboolean success = FALSE;
    GtkWidget *grid_target = GTK_WIDGET (self->view_ui);

    if (self->group_by_type)
    {
        GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));
        GtkWidget *focus = root != NULL ? gtk_root_get_focus (root) : NULL;

        for (GtkWidget *widget = focus; widget != NULL; widget = gtk_widget_get_parent (widget))
        {
            if (GTK_IS_GRID_VIEW (widget))
            {
                grid_target = widget;
                break;
            }
            if (widget == GTK_WIDGET (self))
                break;
        }
    }

    /* We want the same behavior as when the user presses the arrow keys while
     * the focus is in the view. So, let's get the matching arrow key. */
    switch (direction)
    {
        case GTK_DIR_UP:
        {
            direction_keyval = GDK_KEY_Up;
        }
        break;

        case GTK_DIR_DOWN:
        {
            direction_keyval = GDK_KEY_Down;
        }
        break;

        case GTK_DIR_LEFT:
        {
            direction_keyval = GDK_KEY_Left;
        }
        break;

        case GTK_DIR_RIGHT:
        {
            direction_keyval = GDK_KEY_Right;
        }
        break;

        default:
        {
            g_return_if_reached ();
        }
    }

    /* We cannot simulate a click, but we can find the shortcut it triggers and
     * activate its action programatically.
     *
     * First, we create out would-be trigger.*/
    direction_trigger = gtk_keyval_trigger_new (direction_keyval, 0);

    /* Then we iterate over the shortcut installed in GtkGridView until we find
     * a matching trigger. There may be multiple shortcut controllers, and each
     * shortcut controller may hold multiple shortcuts each. Let's loop. */
    controllers = gtk_widget_observe_controllers (grid_target);
    for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
    {
        g_autoptr (GtkEventController) controller = g_list_model_get_item (controllers, i);

        if (!GTK_IS_SHORTCUT_CONTROLLER (controller))
        {
            continue;
        }

        for (guint j = 0; j < g_list_model_get_n_items (G_LIST_MODEL (controller)); j++)
        {
            g_autoptr (GtkShortcut) shortcut = g_list_model_get_item (G_LIST_MODEL (controller), j);
            GtkShortcutTrigger *trigger = gtk_shortcut_get_trigger (shortcut);

            if (gtk_shortcut_trigger_equal (trigger, direction_trigger))
            {
                /* Match found. Activate the action to move cursor. */
                success = gtk_shortcut_action_activate (gtk_shortcut_get_action (shortcut),
                                                        0,
                                                        grid_target,
                                                        gtk_shortcut_get_arguments (shortcut));
                break;
            }
        }
    }

    /* If the hack fails (GTK may change it's internal behavior), fallback. */
    if (!success)
    {
        g_warning_once ("GTK shortcut behavior has changed, manual shortcut method failed");
        NAUTILUS_LIST_BASE_CLASS (nautilus_grid_view_parent_class)->preview_selection_event (list_base, direction);
    }
}

/* We only care about the keyboard activation part that GtkGridView provides,
 * but we don't need any special filtering here. Indeed, we ask GtkGridView
 * to not activate on single click, and we get to handle double clicks before
 * GtkGridView does (as one of widget subclassing's goal is to modify the parent
 * class's behavior), while claiming the click gestures, so it means GtkGridView
 * will never react to a click event to emit this signal. So we should be pretty
 * safe here with regards to our custom item click handling.
 */
static void
on_grid_view_item_activated (GtkGridView *grid_view,
                             guint        position,
                             gpointer     user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), position);
    g_autoptr (NautilusViewItem) item = row ? gtk_tree_list_row_get_item (row) : NULL;

    if (item != NULL && nautilus_view_item_is_group (item))
    {
        gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
        return;
    }

    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);
}

static guint
real_get_icon_size (NautilusListBase *list_base_view)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base_view);

    return get_icon_size_for_zoom_level (self->zoom_level);
}

static int
real_get_zoom_level (NautilusListBase *list_base_view)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base_view);

    return self->zoom_level;
}

static void
real_scroll_to (NautilusListBase   *list_base_view,
                guint               position,
                GtkListScrollFlags  flags,
                GtkScrollInfo      *scroll)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base_view);

    if (!self->group_by_type)
    {
        gtk_grid_view_scroll_to (self->view_ui, position, flags, scroll);
        return;
    }

    NautilusViewModel *model = nautilus_list_base_get_model (list_base_view);
    g_autoptr (GtkTreeListRow) row = model != NULL ? g_list_model_get_item (G_LIST_MODEL (model), position) : NULL;
    g_autoptr (NautilusViewItem) item = row != NULL ? gtk_tree_list_row_get_item (row) : NULL;
    guint group_position = 0;
    guint child_position = 0;

    if (item == NULL ||
        !nautilus_view_model_find_group_item (model, item, &group_position, &child_position))
        return;

    if ((flags & GTK_LIST_SCROLL_SELECT) != 0)
        gtk_selection_model_select_item (GTK_SELECTION_MODEL (model), position, TRUE);

    GtkWidget *group_widget = gtk_widget_get_first_child (GTK_WIDGET (self->group_view_ui));
    for (guint i = 0; group_widget != NULL && i < group_position; i++)
        group_widget = gtk_widget_get_next_sibling (group_widget);

    if (group_widget != NULL)
    {
        graphene_rect_t bounds;
        if (gtk_widget_compute_bounds (group_widget,
                                       GTK_WIDGET (self->group_view_ui),
                                       &bounds))
        {
            GtkScrolledWindow *scrolled = GTK_SCROLLED_WINDOW (nautilus_list_base_get_scrolled_window (list_base_view));
            GtkAdjustment *vadjustment = gtk_scrolled_window_get_vadjustment (scrolled);
            gtk_adjustment_clamp_page (vadjustment,
                                       bounds.origin.y,
                                       bounds.origin.y + MAX (1.0f, bounds.size.height));
        }
    }

    if ((flags & GTK_LIST_SCROLL_FOCUS) != 0)
    {
        GtkWidget *cell = nautilus_view_item_get_item_ui (item);
        if (cell != NULL)
            gtk_widget_grab_focus (cell);
    }
}

static GVariant *
real_get_sort_state (NautilusListBase *list_base)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);

    return g_variant_take_ref (g_variant_new ("(sb)",
                                              g_quark_to_string (self->sort_attribute),
                                              self->reversed));
}

static void
set_rubberband_recursively (GtkWidget *widget,
                            gboolean   enabled)
{
    if (GTK_IS_GRID_VIEW (widget))
        gtk_grid_view_set_enable_rubberband (GTK_GRID_VIEW (widget), enabled);

    for (GtkWidget *child = gtk_widget_get_first_child (widget);
         child != NULL;
         child = gtk_widget_get_next_sibling (child))
        set_rubberband_recursively (child, enabled);
}

static void
real_set_enable_rubberband (NautilusListBase *list_base,
                            gboolean          enabled)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);

    self->rubberband_enabled = enabled;
    gtk_grid_view_set_enable_rubberband (self->view_ui, enabled);
    if (self->group_view_ui != NULL)
        set_rubberband_recursively (GTK_WIDGET (self->group_view_ui), enabled);
}

static void
real_set_sort_state (NautilusListBase *list_base,
                     GVariant         *value)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (list_base);
    const gchar *target_name;
    NautilusViewModel *model = nautilus_list_base_get_model (list_base);
    g_autoptr (GtkCustomSorter) sorter = NULL;

    /* Sort state binding should be set after the model is set.*/
    g_return_if_fail (model != NULL);

    g_variant_get (value, "(&sb)", &target_name, &self->reversed);
    self->sort_attribute = g_quark_from_string (target_name);

    sorter = gtk_custom_sorter_new (nautilus_grid_view_sort, self, NULL);
    nautilus_view_model_set_sorter (model, GTK_SORTER (sorter));
}

static void
on_captions_preferences_changed (NautilusGridView *self)
{
    set_captions_from_preferences (self);

    /* Hack: this relies on the assumption that NautilusGridCell updates
     * captions whenever the icon size is set (even if it's the same value). */
    g_object_notify (G_OBJECT (self), "icon-size");
}

static void
dispose (GObject *object)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (object);
    GtkWidget *scrolled_window = nautilus_list_base_get_scrolled_window (NAUTILUS_LIST_BASE (self));

    if (self->view_ui != NULL)
        gtk_grid_view_set_model (self->view_ui, NULL);
    if (scrolled_window != NULL)
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), NULL);

    if (self->group_roots_model != NULL)
        g_signal_handlers_disconnect_by_data (self->group_roots_model, self);

    g_clear_object (&self->view_ui);
    g_clear_object (&self->group_view_ui);
    g_clear_object (&self->group_roots_model);

    G_OBJECT_CLASS (nautilus_grid_view_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
    G_OBJECT_CLASS (nautilus_grid_view_parent_class)->finalize (object);
}

/* Flat (ungrouped) grid --------------------------------------------------- */
static void
bind_cell (GtkSignalListItemFactory *factory,
           GtkListItem              *listitem,
           gpointer                  user_data)
{
    GtkWidget *cell;
    g_autoptr (NautilusViewItem) item = NULL;

    cell = gtk_list_item_get_child (listitem);
    item = get_view_item (listitem);
    g_return_if_fail (item != NULL);

    nautilus_view_item_set_item_ui (item, cell);

    if (nautilus_view_cell_once (NAUTILUS_VIEW_CELL (cell)))
    {
        GtkWidget *parent;

        parent = gtk_widget_get_parent (cell);
        gtk_widget_set_halign (parent, GTK_ALIGN_CENTER);
        gtk_widget_set_valign (parent, GTK_ALIGN_START);
    }
}

static void
unbind_cell (GtkSignalListItemFactory *factory,
             GtkListItem              *listitem,
             gpointer                  user_data)
{
    g_autoptr (NautilusViewItem) item = get_view_item (listitem);

    if (item != NULL)
        nautilus_view_item_set_item_ui (item, NULL);
}

static void
setup_cell (GtkSignalListItemFactory *factory,
            GtkListItem              *listitem,
            gpointer                  user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    NautilusGridCell *cell;
    GtkExpression *expression;

    cell = nautilus_grid_cell_new (NAUTILUS_LIST_BASE (self));
    gtk_list_item_set_child (listitem, GTK_WIDGET (cell));
    setup_cell_common (G_OBJECT (listitem), NAUTILUS_VIEW_CELL (cell));
    setup_cell_hover (NAUTILUS_VIEW_CELL (cell));

    g_object_bind_property (self, "icon-size",
                            cell, "icon-size",
                            G_BINDING_SYNC_CREATE);

    nautilus_grid_cell_set_caption_attributes (cell, self->caption_attributes);

    expression = gtk_property_expression_new (GTK_TYPE_LIST_ITEM, NULL, "item");
    expression = gtk_property_expression_new (GTK_TYPE_TREE_LIST_ROW, expression, "item");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_VIEW_ITEM, expression, "file");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_FILE, expression, "a11y-name");
    gtk_expression_bind (expression, listitem, "accessible-label", listitem);
}

/* Grouped grid: vertical groups, one dedicated grid per group ------------- */
static void
bind_group_file_cell (GtkSignalListItemFactory *factory,
                      GtkListItem              *listitem,
                      gpointer                  user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    NautilusViewItem *item = NAUTILUS_VIEW_ITEM (gtk_list_item_get_item (listitem));
    NautilusGridCell *cell = NAUTILUS_GRID_CELL (gtk_list_item_get_child (listitem));
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));

    g_return_if_fail (item != NULL);
    g_return_if_fail (!nautilus_view_item_is_group (item));

    nautilus_view_item_set_item_ui (item, GTK_WIDGET (cell));

    guint global_position = model != NULL ?
                            nautilus_view_model_get_position_for_item (model, item) :
                            GTK_INVALID_LIST_POSITION;
    g_object_set (cell, "position", global_position, NULL);

    if (nautilus_view_cell_once (NAUTILUS_VIEW_CELL (cell)))
    {
        GtkWidget *parent = gtk_widget_get_parent (GTK_WIDGET (cell));
        gtk_widget_set_halign (parent, GTK_ALIGN_CENTER);
        gtk_widget_set_valign (parent, GTK_ALIGN_START);
    }
}

static void
unbind_group_file_cell (GtkSignalListItemFactory *factory,
                        GtkListItem              *listitem,
                        gpointer                  user_data)
{
    NautilusViewItem *item = NAUTILUS_VIEW_ITEM (gtk_list_item_get_item (listitem));

    if (item != NULL)
        nautilus_view_item_set_item_ui (item, NULL);
}

static void
setup_group_file_cell (GtkSignalListItemFactory *factory,
                       GtkListItem              *listitem,
                       gpointer                  user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    NautilusGridCell *cell = nautilus_grid_cell_new (NAUTILUS_LIST_BASE (self));
    GtkExpression *expression;

    gtk_list_item_set_child (listitem, GTK_WIDGET (cell));
    setup_cell_common_direct (G_OBJECT (listitem), NAUTILUS_VIEW_CELL (cell));
    setup_cell_hover (NAUTILUS_VIEW_CELL (cell));

    g_object_bind_property (self, "icon-size",
                            cell, "icon-size",
                            G_BINDING_SYNC_CREATE);
    nautilus_grid_cell_set_caption_attributes (cell, self->caption_attributes);

    /* listitem:item is a NautilusViewItem directly in a per-group grid. */
    expression = gtk_property_expression_new (GTK_TYPE_LIST_ITEM, NULL, "item");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_VIEW_ITEM, expression, "file");
    expression = gtk_property_expression_new (NAUTILUS_TYPE_FILE, expression, "a11y-name");
    gtk_expression_bind (expression, listitem, "accessible-label", listitem);
}

static GtkListItemFactory *
create_group_file_factory (NautilusGridView *self)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();

    g_signal_connect (factory, "setup", G_CALLBACK (setup_group_file_cell), self);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_group_file_cell), self);
    g_signal_connect (factory, "unbind", G_CALLBACK (unbind_group_file_cell), self);

    return factory;
}

static void
on_group_grid_item_activated (GtkGridView *grid,
                              guint        position,
                              gpointer     user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);
    nautilus_list_base_activate_selection (NAUTILUS_LIST_BASE (self), FALSE);
}

typedef struct
{
    NautilusGridView *view; /* unowned */
    GtkToggleButton *toggle;
    GtkImage *arrow;
    GtkLabel *label;
    GtkGridView *grid;
    NautilusViewItem *group;
    GtkSelectionModel *selection_model;
    GBinding *title_binding;
    gboolean updating;
} GroupRowData;

static void
update_group_arrow (GroupRowData *data)
{
    gtk_image_set_from_icon_name (data->arrow,
                                  gtk_toggle_button_get_active (data->toggle) ?
                                  "pan-down-symbolic" : "pan-end-symbolic");
}

static void
on_group_toggle (GtkToggleButton *toggle,
                 gpointer         user_data)
{
    GroupRowData *data = user_data;

    if (data->updating || data->group == NULL)
        return;

    gboolean expanded = gtk_toggle_button_get_active (toggle);
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (data->view));
    const char *key = nautilus_view_item_get_group_key (data->group);

    if (model != NULL && key != NULL)
        nautilus_view_model_set_group_collapsed (model, key, !expanded);

    gtk_widget_set_visible (GTK_WIDGET (data->grid), expanded);
    update_group_arrow (data);
}

static void
clear_group_row (GroupRowData *data)
{
    if (data->title_binding != NULL)
    {
        g_binding_unbind (data->title_binding);
        g_clear_object (&data->title_binding);
    }

    gtk_grid_view_set_model (data->grid, NULL);
    g_clear_object (&data->selection_model);
    g_clear_object (&data->group);
}

static void
group_row_data_free (gpointer user_data)
{
    GroupRowData *data = user_data;
    clear_group_row (data);
    g_free (data);
}

static GtkWidget *
create_group_block (NautilusGridView *self,
                    NautilusViewItem *group)
{
    GroupRowData *data = g_new0 (GroupRowData, 1);
    GtkWidget *container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *header = gtk_toggle_button_new ();
    GtkWidget *header_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *arrow = gtk_image_new_from_icon_name ("pan-down-symbolic");
    GtkWidget *label = gtk_label_new (NULL);
    GtkWidget *grid = gtk_grid_view_new (NULL, create_group_file_factory (self));
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));

    data->view = self;
    data->toggle = GTK_TOGGLE_BUTTON (header);
    data->arrow = GTK_IMAGE (arrow);
    data->label = GTK_LABEL (label);
    data->grid = GTK_GRID_VIEW (grid);
    data->group = g_object_ref (group);

    gtk_widget_add_css_class (container, "group-block");
    gtk_widget_set_hexpand (container, TRUE);
    gtk_widget_set_vexpand (container, FALSE);
    gtk_widget_set_valign (container, GTK_ALIGN_START);

    gtk_widget_add_css_class (header, "flat");
    gtk_widget_set_halign (header, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand (header, TRUE);
    gtk_widget_set_margin_top (header, 6);
    gtk_widget_set_margin_start (header, 8);
    gtk_widget_set_margin_end (header, 8);

    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class (label, "heading");
    gtk_widget_set_hexpand (label, TRUE);

    gtk_box_append (GTK_BOX (header_box), arrow);
    gtk_box_append (GTK_BOX (header_box), label);
    gtk_button_set_child (GTK_BUTTON (header), header_box);

    gtk_grid_view_set_single_click_activate (GTK_GRID_VIEW (grid), FALSE);
    gtk_grid_view_set_min_columns (GTK_GRID_VIEW (grid), 1);
    gtk_grid_view_set_max_columns (GTK_GRID_VIEW (grid), 20);
    gtk_grid_view_set_tab_behavior (GTK_GRID_VIEW (grid), GTK_LIST_TAB_ITEM);
    gtk_grid_view_set_enable_rubberband (GTK_GRID_VIEW (grid), self->rubberband_enabled);
    gtk_widget_add_css_class (grid, "group-grid");
    gtk_widget_set_hexpand (grid, TRUE);
    gtk_widget_set_vexpand (grid, FALSE);
    gtk_widget_set_valign (grid, GTK_ALIGN_START);
    g_signal_connect (grid, "activate", G_CALLBACK (on_group_grid_item_activated), self);

    gtk_box_append (GTK_BOX (container), header);
    gtk_box_append (GTK_BOX (container), grid);

    data->title_binding = g_object_ref (g_object_bind_property (group,
                                                                "group-title",
                                                                data->label,
                                                                "label",
                                                                G_BINDING_SYNC_CREATE));

    if (model != NULL)
    {
        GListStore *children = nautilus_view_item_get_group_children (group);
        data->selection_model = nautilus_group_selection_model_new (model, G_LIST_MODEL (children));
        gtk_grid_view_set_model (data->grid, data->selection_model);
    }

    const char *key = nautilus_view_item_get_group_key (group);
    gboolean expanded = model == NULL || !nautilus_view_model_get_group_collapsed (model, key);
    data->updating = TRUE;
    gtk_toggle_button_set_active (data->toggle, expanded);
    data->updating = FALSE;
    gtk_widget_set_visible (grid, expanded);
    update_group_arrow (data);

    g_signal_connect (header, "toggled", G_CALLBACK (on_group_toggle), data);
    g_object_set_data_full (G_OBJECT (container),
                            "nautilus-group-row-data",
                            data,
                            group_row_data_free);

    return container;
}

static GtkGridView *
create_flat_view_ui (NautilusGridView *self)
{
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    GtkWidget *widget;

    g_signal_connect (factory, "setup", G_CALLBACK (setup_cell), self);
    g_signal_connect (factory, "bind", G_CALLBACK (bind_cell), self);
    g_signal_connect (factory, "unbind", G_CALLBACK (unbind_cell), self);

    widget = gtk_grid_view_new (NULL, factory);
    gtk_grid_view_set_single_click_activate (GTK_GRID_VIEW (widget), FALSE);
    gtk_grid_view_set_max_columns (GTK_GRID_VIEW (widget), 20);
    gtk_grid_view_set_tab_behavior (GTK_GRID_VIEW (widget), GTK_LIST_TAB_ITEM);

    gtk_accessible_update_property (GTK_ACCESSIBLE (widget),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    _("Content View"),
                                    GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION,
                                    _("View of the current location"),
                                    -1);
    g_signal_connect (widget, "activate", G_CALLBACK (on_grid_view_item_activated), self);

    return GTK_GRID_VIEW (widget);
}

static GtkBox *
create_group_view_ui (NautilusGridView *self)
{
    GtkWidget *widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

    gtk_widget_add_css_class (widget, "grouped-grid");
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_widget_set_vexpand (widget, FALSE);
    gtk_widget_set_valign (widget, GTK_ALIGN_START);

    gtk_accessible_update_property (GTK_ACCESSIBLE (widget),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL,
                                    _("Content View"),
                                    GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION,
                                    _("Grouped grid view of the current location"),
                                    -1);

    return GTK_BOX (widget);
}

static void
clear_group_blocks (NautilusGridView *self)
{
    GtkWidget *child;

    if (self->group_view_ui == NULL)
        return;

    while ((child = gtk_widget_get_first_child (GTK_WIDGET (self->group_view_ui))) != NULL)
        gtk_box_remove (self->group_view_ui, child);
}

static void
rebuild_group_blocks (NautilusGridView *self)
{
    clear_group_blocks (self);

    if (!self->group_by_type || self->group_roots_model == NULL)
        return;

    guint n = g_list_model_get_n_items (self->group_roots_model);
    for (guint i = 0; i < n; i++)
    {
        g_autoptr (NautilusViewItem) group = g_list_model_get_item (self->group_roots_model, i);
        if (group == NULL || !nautilus_view_item_is_group (group))
            continue;

        gtk_box_append (self->group_view_ui, create_group_block (self, group));
    }

    set_rubberband_recursively (GTK_WIDGET (self->group_view_ui), self->rubberband_enabled);
}

static void
on_group_roots_items_changed (GListModel       *model,
                              guint             position,
                              guint             removed,
                              guint             added,
                              NautilusGridView *self)
{
    rebuild_group_blocks (self);
}

static void
set_group_roots_model (NautilusGridView *self,
                       GListModel       *model)
{
    if (self->group_roots_model == model)
        return;

    if (self->group_roots_model != NULL)
        g_signal_handlers_disconnect_by_func (self->group_roots_model,
                                              on_group_roots_items_changed,
                                              self);
    g_clear_object (&self->group_roots_model);
    if (model != NULL)
    {
        self->group_roots_model = g_object_ref (model);
        g_signal_connect_object (model,
                                 "items-changed",
                                 G_CALLBACK (on_group_roots_items_changed),
                                 self,
                                 0);
    }
}

static void
update_presentation (NautilusGridView *self)
{
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    GtkScrolledWindow *scrolled = GTK_SCROLLED_WINDOW (nautilus_list_base_get_scrolled_window (NAUTILUS_LIST_BASE (self)));

    if (model != NULL)
    {
        nautilus_view_model_set_group_by_type (model, self->group_by_type);
        self->rubberband_enabled = !nautilus_view_model_get_single_selection (model);
    }

    if (self->group_by_type)
    {
        gtk_grid_view_set_model (self->view_ui, NULL);

        g_autoptr (GListModel) groups = model != NULL ?
                                        nautilus_view_model_dup_group_roots_model (model) : NULL;
        set_group_roots_model (self, groups);
        rebuild_group_blocks (self);

        gtk_scrolled_window_set_child (scrolled, GTK_WIDGET (self->group_view_ui));
    }
    else
    {
        set_group_roots_model (self, NULL);
        clear_group_blocks (self);
        gtk_grid_view_set_model (self->view_ui,
                                 model != NULL ? GTK_SELECTION_MODEL (model) : NULL);
        gtk_grid_view_set_enable_rubberband (self->view_ui, self->rubberband_enabled);
        gtk_scrolled_window_set_child (scrolled, GTK_WIDGET (self->view_ui));
    }
}

static void
on_model_changed (NautilusGridView *self)
{
    update_presentation (self);
}

static void
nautilus_grid_view_class_init (NautilusGridViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    NautilusListBaseClass *list_base_view_class = NAUTILUS_LIST_BASE_CLASS (klass);

    object_class->dispose = dispose;
    object_class->finalize = finalize;

    list_base_view_class->get_icon_size = real_get_icon_size;
    list_base_view_class->get_sort_state = real_get_sort_state;
    list_base_view_class->get_view_info = real_get_view_info;
    list_base_view_class->get_zoom_level = real_get_zoom_level;
    list_base_view_class->preview_selection_event = real_preview_selection_event;
    list_base_view_class->scroll_to = real_scroll_to;
    list_base_view_class->set_enable_rubberband = real_set_enable_rubberband;
    list_base_view_class->set_sort_state = real_set_sort_state;
    list_base_view_class->set_zoom_level = real_set_zoom_level;
    list_base_view_class->setup_directory = nautilus_grid_view_setup_directory;
}

static void
nautilus_grid_view_init (NautilusGridView *self)
{
    GtkWidget *scrolled_window = nautilus_list_base_get_scrolled_window (NAUTILUS_LIST_BASE (self));

    self->group_by_type = TRUE;
    self->rubberband_enabled = TRUE;
    gtk_widget_add_css_class (GTK_WIDGET (self), "nautilus-grid-view");

    set_captions_from_preferences (self);
    g_signal_connect_object (nautilus_icon_view_preferences,
                             "changed::" NAUTILUS_PREFERENCES_ICON_VIEW_CAPTIONS,
                             G_CALLBACK (on_captions_preferences_changed),
                             self,
                             G_CONNECT_SWAPPED);


    self->view_ui = g_object_ref_sink (create_flat_view_ui (self));
    self->group_view_ui = g_object_ref_sink (create_group_view_ui (self));
    nautilus_list_base_setup_gestures (NAUTILUS_LIST_BASE (self));

    g_signal_connect_swapped (self, "notify::model", G_CALLBACK (on_model_changed), self);

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window),
                                   GTK_WIDGET (self->group_view_ui));

    g_signal_connect_object (gtk_filechooser_preferences,
                             "changed::" NAUTILUS_PREFERENCES_SORT_DIRECTORIES_FIRST,
                             G_CALLBACK (update_sort_directories_first),
                             self,
                             G_CONNECT_SWAPPED);

    nautilus_list_base_set_zoom_level (NAUTILUS_LIST_BASE (self), get_default_zoom_level ());
}

NautilusGridView *
nautilus_grid_view_new (void)
{
    return g_object_new (NAUTILUS_TYPE_GRID_VIEW, NULL);
}

void
nautilus_grid_view_set_group_by_type (NautilusGridView *self, gboolean enabled)
{
    g_return_if_fail (NAUTILUS_IS_GRID_VIEW (self));

    if (self->group_by_type == enabled)
        return;

    self->group_by_type = enabled;
    update_presentation (self);
}

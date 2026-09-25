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

/* Grouped grid: vertical groups, one non-scrollable GtkFlowBox per group.
 *
 * GtkGridView/GtkListView implement GtkScrollable and are designed to be the
 * direct child of a GtkScrolledWindow. Nesting one such view per group inside
 * another scrolling container gives them an incorrect natural height (often
 * the one-column height), which creates huge blank gaps. GtkFlowBox is a
 * regular height-for-width widget and is therefore appropriate here: it wraps
 * files into rows and requests exactly the height needed for those rows. */
typedef struct
{
    NautilusGridView *view; /* unowned */
    GtkToggleButton *toggle;
    GtkImage *arrow;
    GtkLabel *label;
    GtkFlowBox *flow;
    NautilusViewItem *group;
    NautilusViewModel *model;
    GBinding *title_binding;
    gulong model_selection_handler;
    gulong model_items_handler;
    GdkModifierType selection_modifiers;
    gboolean updating;
    gboolean syncing_selection;
} GroupRowData;

typedef struct
{
    NautilusViewItem *item;
    GtkWidget *cell; /* unowned, valid while destroy notify runs */
} GroupFlowCellData;

static guint
group_flow_item_global_position (GroupRowData     *data,
                                 NautilusViewItem *item)
{
    if (data->model == NULL || item == NULL)
        return GTK_INVALID_LIST_POSITION;

    return nautilus_view_model_get_position_for_item (data->model, item);
}

static void
group_flow_cell_data_free (gpointer user_data)
{
    GroupFlowCellData *data = user_data;

    if (data->item != NULL &&
        nautilus_view_item_get_item_ui (data->item) == data->cell)
        nautilus_view_item_set_item_ui (data->item, NULL);

    g_clear_object (&data->item);
    g_free (data);
}

static GtkWidget *
create_group_flow_cell (gpointer item_object,
                        gpointer user_data)
{
    GroupRowData *data = user_data;
    NautilusViewItem *item = NAUTILUS_VIEW_ITEM (item_object);
    NautilusGridCell *cell = nautilus_grid_cell_new (NAUTILUS_LIST_BASE (data->view));
    guint global_position = group_flow_item_global_position (data, item);
    GroupFlowCellData *cell_data = g_new0 (GroupFlowCellData, 1);

    /* NautilusGridCell updates its icon as soon as an item is bound.
     * Initialise the icon size and captions first; otherwise the bind callback
     * can call nautilus_file_get_icon_paintable() with icon-size == 0. */
    g_object_bind_property (data->view, "icon-size",
                            cell, "icon-size",
                            G_BINDING_SYNC_CREATE);
    nautilus_grid_cell_set_caption_attributes (cell, data->view->caption_attributes);

    setup_cell_common_item (NAUTILUS_VIEW_CELL (cell), item, global_position);
    setup_cell_hover (NAUTILUS_VIEW_CELL (cell));

    gtk_widget_set_halign (GTK_WIDGET (cell), GTK_ALIGN_CENTER);
    gtk_widget_set_valign (GTK_WIDGET (cell), GTK_ALIGN_START);
    nautilus_view_item_set_item_ui (item, GTK_WIDGET (cell));

    cell_data->item = g_object_ref (item);
    cell_data->cell = GTK_WIDGET (cell);
    g_object_set_data_full (G_OBJECT (cell),
                            "nautilus-group-flow-cell-data",
                            cell_data,
                            group_flow_cell_data_free);

    return GTK_WIDGET (cell);
}

static NautilusViewItem *
group_flow_child_get_item (GtkFlowBoxChild *child)
{
    GtkWidget *cell = gtk_flow_box_child_get_child (child);

    if (!NAUTILUS_IS_VIEW_CELL (cell))
        return NULL;

    return nautilus_view_cell_get_item (NAUTILUS_VIEW_CELL (cell));
}

static void
refresh_group_flow_positions (GroupRowData *data)
{
    if (data->flow == NULL || data->model == NULL)
        return;

    for (gint i = 0; ; i++)
    {
        GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (data->flow, i);
        if (child == NULL)
            break;

        GtkWidget *cell = gtk_flow_box_child_get_child (child);
        g_autoptr (NautilusViewItem) item = group_flow_child_get_item (child);
        guint global_position = group_flow_item_global_position (data, item);

        if (NAUTILUS_IS_VIEW_CELL (cell))
            g_object_set (cell, "position", global_position, NULL);
    }
}

static void
sync_group_flow_selection_from_model (GroupRowData *data)
{
    if (data->flow == NULL || data->model == NULL || data->syncing_selection)
        return;

    data->syncing_selection = TRUE;

    for (gint i = 0; ; i++)
    {
        GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (data->flow, i);
        if (child == NULL)
            break;

        g_autoptr (NautilusViewItem) item = group_flow_child_get_item (child);
        guint global_position = group_flow_item_global_position (data, item);
        gboolean selected = global_position != GTK_INVALID_LIST_POSITION &&
                            gtk_selection_model_is_selected (GTK_SELECTION_MODEL (data->model),
                                                             global_position);

        if (selected && !gtk_flow_box_child_is_selected (child))
            gtk_flow_box_select_child (data->flow, child);
        else if (!selected && gtk_flow_box_child_is_selected (child))
            gtk_flow_box_unselect_child (data->flow, child);
    }

    data->syncing_selection = FALSE;
}

static void
on_group_parent_selection_changed (GtkSelectionModel *model,
                                   guint              position,
                                   guint              n_items,
                                   GroupRowData      *data)
{
    sync_group_flow_selection_from_model (data);
}

static void
on_group_parent_items_changed (GListModel   *model,
                               guint         position,
                               guint         removed,
                               guint         added,
                               GroupRowData *data)
{
    refresh_group_flow_positions (data);
    sync_group_flow_selection_from_model (data);
}

static void
on_group_flow_pressed (GtkGestureClick *gesture,
                       gint             n_press,
                       gdouble          x,
                       gdouble          y,
                       gpointer         user_data)
{
    GroupRowData *data = user_data;

    if (gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (gesture)) == GDK_BUTTON_PRIMARY)
        data->selection_modifiers = gtk_event_controller_get_current_event_state (GTK_EVENT_CONTROLLER (gesture));
}

static void
on_group_flow_selection_changed (GtkFlowBox *flow,
                                 gpointer    user_data)
{
    GroupRowData *data = user_data;

    if (data->syncing_selection || data->model == NULL)
        return;

    GdkModifierType modifiers = data->selection_modifiers;
    gboolean preserve_other_groups = (modifiers & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0;

    data->syncing_selection = TRUE;

    if (!preserve_other_groups)
        gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (data->model));

    for (gint i = 0; ; i++)
    {
        GtkFlowBoxChild *child = gtk_flow_box_get_child_at_index (flow, i);
        if (child == NULL)
            break;

        g_autoptr (NautilusViewItem) item = group_flow_child_get_item (child);
        guint global_position = group_flow_item_global_position (data, item);
        if (global_position == GTK_INVALID_LIST_POSITION)
            continue;

        if (gtk_flow_box_child_is_selected (child))
            gtk_selection_model_select_item (GTK_SELECTION_MODEL (data->model),
                                             global_position,
                                             FALSE);
        else if (preserve_other_groups &&
                 gtk_selection_model_is_selected (GTK_SELECTION_MODEL (data->model), global_position))
            gtk_selection_model_unselect_item (GTK_SELECTION_MODEL (data->model), global_position);
    }

    data->syncing_selection = FALSE;
    data->selection_modifiers = 0;
}

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
    const char *key = nautilus_view_item_get_group_key (data->group);

    if (data->model != NULL && key != NULL)
        nautilus_view_model_set_group_collapsed (data->model, key, !expanded);

    gtk_widget_set_visible (GTK_WIDGET (data->flow), expanded);
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

    /* GtkFlowBox owns and tears down its model binding during widget disposal.
     * At this point the child FlowBox may already have been finalized, so an
     * explicit gtk_flow_box_bind_model(NULL) would dereference a stale pointer. */

    if (data->model != NULL)
    {
        if (data->model_selection_handler != 0)
            g_signal_handler_disconnect (data->model, data->model_selection_handler);
        if (data->model_items_handler != 0)
            g_signal_handler_disconnect (data->model, data->model_items_handler);
    }

    g_clear_object (&data->model);
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
    GtkWidget *flow = gtk_flow_box_new ();
    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));

    data->view = self;
    data->toggle = GTK_TOGGLE_BUTTON (header);
    data->arrow = GTK_IMAGE (arrow);
    data->label = GTK_LABEL (label);
    data->flow = GTK_FLOW_BOX (flow);
    data->group = g_object_ref (group);
    data->model = model != NULL ? g_object_ref (model) : NULL;

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

    gtk_orientable_set_orientation (GTK_ORIENTABLE (flow), GTK_ORIENTATION_HORIZONTAL);
    gtk_flow_box_set_activate_on_single_click (GTK_FLOW_BOX (flow), FALSE);
    gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (flow), 1);
    gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (flow), 20);
    gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (flow), 6);
    gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (flow), 6);
    gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (flow), TRUE);
    gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (flow),
                                     model != NULL && nautilus_view_model_get_single_selection (model) ?
                                     GTK_SELECTION_SINGLE : GTK_SELECTION_MULTIPLE);
    gtk_widget_add_css_class (flow, "group-flow");
    gtk_widget_set_hexpand (flow, TRUE);
    gtk_widget_set_vexpand (flow, FALSE);
    gtk_widget_set_valign (flow, GTK_ALIGN_START);

    GtkGesture *selection_gesture = gtk_gesture_click_new ();
    gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (selection_gesture), GTK_PHASE_CAPTURE);
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (selection_gesture), GDK_BUTTON_PRIMARY);
    g_signal_connect (selection_gesture, "pressed", G_CALLBACK (on_group_flow_pressed), data);
    gtk_widget_add_controller (flow, GTK_EVENT_CONTROLLER (selection_gesture));

    g_signal_connect (flow, "selected-children-changed",
                      G_CALLBACK (on_group_flow_selection_changed), data);
    gtk_box_append (GTK_BOX (container), header);
    gtk_box_append (GTK_BOX (container), flow);

    data->title_binding = g_object_ref (g_object_bind_property (group,
                                                                "group-title",
                                                                data->label,
                                                                "label",
                                                                G_BINDING_SYNC_CREATE));

    if (model != NULL)
    {
        GListStore *children = nautilus_view_item_get_group_children (group);
        gtk_flow_box_bind_model (data->flow,
                                 G_LIST_MODEL (children),
                                 create_group_flow_cell,
                                 data,
                                 NULL);
        data->model_selection_handler =
            g_signal_connect (model, "selection-changed",
                              G_CALLBACK (on_group_parent_selection_changed), data);
        data->model_items_handler =
            g_signal_connect (model, "items-changed",
                              G_CALLBACK (on_group_parent_items_changed), data);
        refresh_group_flow_positions (data);
        sync_group_flow_selection_from_model (data);
    }

    const char *key = nautilus_view_item_get_group_key (group);
    gboolean expanded = model == NULL || !nautilus_view_model_get_group_collapsed (model, key);
    data->updating = TRUE;
    gtk_toggle_button_set_active (data->toggle, expanded);
    data->updating = FALSE;
    gtk_widget_set_visible (flow, expanded);
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

static gboolean
on_group_view_key_pressed (GtkEventControllerKey *controller,
                           guint                  keyval,
                           guint                  keycode,
                           GdkModifierType        state,
                           gpointer               user_data)
{
    NautilusGridView *self = NAUTILUS_GRID_VIEW (user_data);

    if ((state & GDK_CONTROL_MASK) == 0 || (keyval != GDK_KEY_a && keyval != GDK_KEY_A))
        return FALSE;

    NautilusViewModel *model = nautilus_list_base_get_model (NAUTILUS_LIST_BASE (self));
    if (model == NULL)
        return FALSE;

    if ((state & GDK_SHIFT_MASK) != 0)
        gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (model));
    else
        gtk_selection_model_select_all (GTK_SELECTION_MODEL (model));

    return TRUE;
}

static GtkBox *
create_group_view_ui (NautilusGridView *self)
{
    GtkWidget *widget = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);

    gtk_widget_add_css_class (widget, "grouped-grid");
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_widget_set_vexpand (widget, FALSE);
    gtk_widget_set_valign (widget, GTK_ALIGN_START);

    GtkEventController *key_controller = GTK_EVENT_CONTROLLER (gtk_event_controller_key_new ());
    gtk_event_controller_set_propagation_phase (key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect (key_controller, "key-pressed", G_CALLBACK (on_group_view_key_pressed), self);
    gtk_widget_add_controller (widget, key_controller);

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

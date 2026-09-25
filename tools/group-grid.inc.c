/* SPDX-License-Identifier: GPL-3.0-or-later */
static GtkWidget *
grid_file_cell (GtkListItem *listitem)
{
    GtkWidget *child = gtk_list_item_get_child (listitem);
    return GTK_IS_STACK (child) ? gtk_stack_get_child_by_name (GTK_STACK (child), "file") : child;
}

static GtkTreeExpander *
grid_group_expander (GtkListItem *listitem)
{
    return g_object_get_data (G_OBJECT (listitem), "group-expander");
}

static void
clear_grid_group_binding (GtkListItem *listitem)
{
    GBinding *binding = g_object_get_data (G_OBJECT (listitem), "group-title-binding");
    if (binding != NULL)
    {
        g_binding_unbind (binding);
        g_object_set_data (G_OBJECT (listitem), "group-title-binding", NULL);
    }

    GtkTreeExpander *expander = grid_group_expander (listitem);
    if (expander != NULL)
        gtk_tree_expander_set_list_row (expander, NULL);
}

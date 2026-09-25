/* SPDX-License-Identifier: GPL-3.0-or-later */
static GtkWidget *
file_cell (GtkColumnViewCell *listitem)
{
    GtkWidget *child = gtk_column_view_cell_get_child (listitem);
    return GTK_IS_STACK (child) ? gtk_stack_get_child_by_name (GTK_STACK (child), "file") : child;
}
static void
setup_group_wrapper (GtkSignalListItemFactory *factory, GtkColumnViewCell *listitem, gpointer user_data)
{
    NautilusListView *self = user_data;
    NautilusColumn *column = g_hash_table_lookup (self->factory_to_column_map, factory);
    g_autofree char *name = NULL;
    g_object_get (column, "name", &name, NULL);
    GtkWidget *cell = gtk_column_view_cell_get_child (listitem);
    GtkWidget *stack = gtk_stack_new ();
    gtk_stack_set_hhomogeneous (GTK_STACK (stack), FALSE);
    gtk_stack_set_vhomogeneous (GTK_STACK (stack), FALSE);
    g_object_ref (cell);
    gtk_column_view_cell_set_child (listitem, NULL);
    gtk_stack_add_named (GTK_STACK (stack), cell, "file");
    g_object_unref (cell);
    GtkWidget *group;
    if (g_str_equal (name, "name"))
    {
        group = gtk_tree_expander_new ();
        GtkWidget *label = gtk_label_new (NULL);
        gtk_label_set_xalign (GTK_LABEL (label), 0.0);
        gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class (label, "heading");
        gtk_widget_set_margin_top (label, 8);
        gtk_widget_set_margin_bottom (label, 8);
        gtk_tree_expander_set_child (GTK_TREE_EXPANDER (group), label);
        g_object_set_data (G_OBJECT (listitem), "group-expander", group);
    }
    else group = gtk_label_new ("");
    gtk_stack_add_named (GTK_STACK (stack), group, "group");
    gtk_column_view_cell_set_child (listitem, stack);
}
static void
bind_group_wrapper (GtkSignalListItemFactory *factory, GtkColumnViewCell *listitem, gpointer user_data)
{
    g_autoptr (NautilusViewItem) item = get_view_item (listitem);
    gboolean is_group = nautilus_view_item_is_group (item);
    GtkWidget *stack = gtk_column_view_cell_get_child (listitem);
    gtk_stack_set_visible_child_name (GTK_STACK (stack), is_group ? "group" : "file");
    GtkTreeExpander *expander = g_object_get_data (G_OBJECT (listitem), "group-expander");
    if (expander != NULL)
    {
        gtk_tree_expander_set_list_row (expander, is_group ? gtk_column_view_cell_get_item (listitem) : NULL);
        if (is_group)
        {
            GBinding *binding = g_object_bind_property (item, "group-title", gtk_tree_expander_get_child (expander), "label", G_BINDING_SYNC_CREATE);
            g_object_set_data_full (G_OBJECT (listitem), "group-title-binding", g_object_ref (binding), g_object_unref);
        }
    }
}
static void
unbind_group_wrapper (GtkSignalListItemFactory *factory, GtkColumnViewCell *listitem, gpointer user_data)
{
    GBinding *binding = g_object_get_data (G_OBJECT (listitem), "group-title-binding");
    if (binding != NULL)
    {
        g_binding_unbind (binding);
        g_object_set_data (G_OBJECT (listitem), "group-title-binding", NULL);
    }
    GtkTreeExpander *expander = g_object_get_data (G_OBJECT (listitem), "group-expander");
    if (expander != NULL) gtk_tree_expander_set_list_row (expander, NULL);
}
static void
bind_group_row (GtkSignalListItemFactory *factory, GtkColumnViewRow *row, gpointer user_data)
{
    GtkTreeListRow *tree_row = gtk_column_view_row_get_item (row);
    g_autoptr (NautilusViewItem) item = gtk_tree_list_row_get_item (tree_row);
    gboolean is_group = nautilus_view_item_is_group (item);
    gtk_column_view_row_set_selectable (row, !is_group);
    gtk_column_view_row_set_activatable (row, !is_group);
    if (is_group)
    {
        g_autofree char *title = NULL;
        g_object_get (item, "group-title", &title, NULL);
        gtk_column_view_row_set_accessible_label (row, title);
    }
}

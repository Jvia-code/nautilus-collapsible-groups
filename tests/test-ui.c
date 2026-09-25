/* Real group cell callbacks from NautilusListView, in a small GTK harness.
 * This is not the complete Nautilus application. Files are test doubles.
 */
#define main model_tests_main
#include "test-groups.c"
#undef main

typedef GtkWidget NautilusColumn;
typedef struct { GHashTable *factory_to_column_map; } NautilusListView;
#define get_view_item(cell) (NAUTILUS_VIEW_ITEM (gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (gtk_column_view_cell_get_item (cell)))))
#include "group-cells.inc.c"
static void setup_plain(GtkSignalListItemFactory *f,GtkColumnViewCell *cell,gpointer data)
{
 GtkWidget *label=gtk_label_new("");gtk_label_set_xalign(GTK_LABEL(label),0);
 gtk_widget_set_margin_start(label,24);gtk_column_view_cell_set_child(cell,label);
}
static void bind_plain(GtkSignalListItemFactory *f,GtkColumnViewCell *cell,gpointer data)
{
 g_autoptr(NautilusViewItem) item=get_view_item(cell);
 if(nautilus_view_item_is_group(item))return;
 NautilusFile *file=nautilus_view_item_get_file(item);
 g_autofree char *text=g_strdup_printf("Fichier de test — %s — date %d",(char*)g_object_get_data(G_OBJECT(file),"mime"),GPOINTER_TO_INT(g_object_get_data(G_OBJECT(file),"date")));
 gtk_label_set_text(GTK_LABEL(file_cell(cell)),text);
}
static GtkTreeExpander *find_expander(GtkWidget *widget,const char *key)
{
 if(GTK_IS_TREE_EXPANDER(widget))
 {
  GtkTreeListRow *row=gtk_tree_expander_get_list_row(GTK_TREE_EXPANDER(widget));
  g_autoptr(NautilusViewItem) item=row?gtk_tree_list_row_get_item(row):NULL;
  if(item&&g_strcmp0(nautilus_view_item_get_group_key(item),key)==0)return GTK_TREE_EXPANDER(widget);
 }
 for(GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))
 {
  GtkTreeExpander *found=find_expander(child,key);if(found)return found;
 }
 return NULL;
}
static void settle(void)
{
 gint64 end=g_get_monotonic_time()+300000;
 do {while(g_main_context_iteration(NULL,FALSE));g_usleep(1000);}while(g_get_monotonic_time()<end);
}
int main(int argc,char **argv)
{
 gtk_init();parent_file=g_object_new(NAUTILUS_TYPE_FILE,NULL);
 g_autoptr(NautilusViewModel) model=sample(FALSE);
 GtkWidget *window=gtk_window_new();gtk_window_set_title(GTK_WINDOW(window),"Test GTK des groupes — application Nautilus complète non lancée");gtk_window_set_default_size(GTK_WINDOW(window),880,440);
 GtkWidget *view=gtk_column_view_new(g_object_ref(GTK_SELECTION_MODEL(model)));
 GtkListItemFactory *row_factory=gtk_signal_list_item_factory_new();
 g_signal_connect(row_factory,"bind",G_CALLBACK(bind_group_row),NULL);
 gtk_column_view_set_row_factory(GTK_COLUMN_VIEW(view),row_factory);g_object_unref(row_factory);
 NautilusListView self={g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_object_unref)};
 GtkListItemFactory *factory=gtk_signal_list_item_factory_new();
 GtkWidget *column_info=gtk_label_new(NULL);g_object_ref_sink(column_info);gtk_widget_set_name(column_info,"name");
 g_hash_table_insert(self.factory_to_column_map,factory,column_info);
 g_signal_connect(factory,"setup",G_CALLBACK(setup_plain),NULL);
 g_signal_connect(factory,"setup",G_CALLBACK(setup_group_wrapper),&self);
 g_signal_connect(factory,"bind",G_CALLBACK(bind_plain),NULL);
 g_signal_connect(factory,"bind",G_CALLBACK(bind_group_wrapper),&self);
 g_signal_connect(factory,"unbind",G_CALLBACK(unbind_group_wrapper),&self);
 GtkColumnViewColumn *column=gtk_column_view_column_new("Groupes par type — tri décroissant dans chaque groupe",factory);
 gtk_column_view_column_set_expand(column,TRUE);gtk_column_view_append_column(GTK_COLUMN_VIEW(view),column);g_object_unref(column);
 gtk_window_set_child(GTK_WINDOW(window),view);gtk_window_present(GTK_WINDOW(window));settle();
 GtkTreeExpander *expander=find_expander(view,"application/pdf");g_assert_nonnull(expander);
 gtk_selection_model_select_all(GTK_SELECTION_MODEL(model));assert_only_files_selected(model,4);
 g_assert_true(gtk_widget_activate_action(GTK_WIDGET(expander),"listitem.collapse",NULL));settle();
 g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(model)),==,5);assert_only_files_selected(model,2);
 expander=find_expander(view,"application/pdf");g_assert_nonnull(expander);
 g_assert_true(gtk_widget_activate_action(GTK_WIDGET(expander),"listitem.expand",NULL));settle();
 g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(model)),==,7);assert_only_files_selected(model,2);
 for(int i=0;i<5;i++) {nautilus_view_model_set_group_by_type(model,FALSE);settle();nautilus_view_model_set_group_by_type(model,TRUE);settle();}
 gtk_window_destroy(GTK_WINDOW(window));settle();g_hash_table_unref(self.factory_to_column_map);g_object_unref(parent_file);
 g_print("UI PASS: real GTK factories, collapse/expand actions, selection exclusion, 5 mode switches, teardown.\n");return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real GTK models and unmodified patched NautilusViewModel/ViewItem sources;
 * only the filesystem layer is a deterministic test double.
 */
#include "nautilus-view-model.h"
#include "nautilus-view-item.h"
#include "nautilus-file.h"
#include "nautilus-directory.h"

G_DEFINE_TYPE (NautilusFile, nautilus_file, G_TYPE_OBJECT)
static void nautilus_file_class_init (NautilusFileClass *klass) {}
static void nautilus_file_init (NautilusFile *self) {}
static NautilusFile *parent_file;
NautilusFile *nautilus_file_get_parent (NautilusFile *file) { return g_object_ref (parent_file); }
gboolean nautilus_file_is_directory (NautilusFile *file) { return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (file), "directory")); }
const char *nautilus_file_get_mime_type (NautilusFile *file) { return g_object_get_data (G_OBJECT (file), "mime"); }
NautilusFile *nautilus_directory_get_corresponding_file (NautilusDirectory *directory) { return g_object_ref (parent_file); }

static NautilusViewItem *make_item (const char *mime, int date, gboolean dir)
{
    g_autoptr (NautilusFile) file = g_object_new (NAUTILUS_TYPE_FILE, NULL);
    g_object_set_data_full (G_OBJECT (file), "mime", g_strdup (mime), g_free);
    g_object_set_data (G_OBJECT (file), "date", GINT_TO_POINTER (date));
    g_object_set_data (G_OBJECT (file), "directory", GINT_TO_POINTER (dir));
    return nautilus_view_item_new (file);
}
static gint date_sort (gconstpointer a, gconstpointer b, gpointer unused)
{
    NautilusFile *fa = nautilus_view_item_get_file ((gpointer)a);
    NautilusFile *fb = nautilus_view_item_get_file ((gpointer)b);
    return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (fb), "date")) - GPOINTER_TO_INT (g_object_get_data (G_OBJECT (fa), "date"));
}
static NautilusViewItem *at (NautilusViewModel *model, guint i)
{
    g_autoptr (GtkTreeListRow) row = g_list_model_get_item (G_LIST_MODEL (model), i);
    return row ? gtk_tree_list_row_get_item (row) : NULL;
}
static GtkTreeListRow *group_row (NautilusViewModel *model, const char *key)
{
    for (guint i=0;i<g_list_model_get_n_items(G_LIST_MODEL(model));i++)
    {
        g_autoptr (NautilusViewItem) item=at(model,i);
        if (g_strcmp0 (nautilus_view_item_get_group_key (item), key)==0)
            return g_list_model_get_item(G_LIST_MODEL(model),i);
    }
    return NULL;
}
static void assert_only_files_selected (NautilusViewModel *model, guint expected)
{
    g_autoptr (GtkBitset) bits=gtk_selection_model_get_selection(GTK_SELECTION_MODEL(model));
    g_assert_cmpuint(gtk_bitset_get_size(bits),==,expected);
    for(guint i=0;i<gtk_bitset_get_size(bits);i++)
    {
        g_autoptr(NautilusViewItem) item=at(model,gtk_bitset_get_nth(bits,i));
        g_assert_false(nautilus_view_item_is_group(item));
        g_assert_nonnull(nautilus_view_item_get_file(item));
    }
}
static NautilusViewModel *sample (gboolean single)
{
    NautilusViewModel *model=nautilus_view_model_new(single);
    g_autoptr(GtkCustomSorter) sorter=gtk_custom_sorter_new(date_sort,NULL,NULL);
    nautilus_view_model_set_sorter(model,GTK_SORTER(sorter));
    g_autoptr(NautilusViewItem) a=make_item("application/pdf",10,FALSE);
    g_autoptr(NautilusViewItem) b=make_item("image/png",30,FALSE);
    g_autoptr(NautilusViewItem) c=make_item("application/pdf",20,FALSE);
    g_autoptr(NautilusViewItem) d=make_item("inode/directory",5,TRUE);
    GList *items=NULL;
    items=g_list_append(items,a);items=g_list_append(items,b);items=g_list_append(items,c);items=g_list_append(items,d);
    nautilus_view_model_add_items(model,items);g_list_free(items);
    nautilus_view_model_set_group_by_type(model,TRUE);
    return model;
}
static void test_group_sort (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,7);
    g_autoptr(GtkTreeListRow) row=group_row(m,"application/pdf");g_assert_nonnull(row);
    guint pos=gtk_tree_list_row_get_position(row);
    g_autoptr(NautilusViewItem) newer=at(m,pos+1),older=at(m,pos+2);
    g_assert_cmpint(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(nautilus_view_item_get_file(newer)),"date")),==,20);
    g_assert_cmpint(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(nautilus_view_item_get_file(older)),"date")),==,10);
}
static void test_collapse_selection (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));assert_only_files_selected(m,4);
    g_autoptr(GtkTreeListRow) row=group_row(m,"application/pdf");
    gtk_tree_list_row_set_expanded(row,FALSE);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,5);
    assert_only_files_selected(m,2);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));assert_only_files_selected(m,2);
    gtk_tree_list_row_set_expanded(row,TRUE);assert_only_files_selected(m,2);
}
static void test_no_headers (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_assert_false(gtk_selection_model_select_item(GTK_SELECTION_MODEL(m),0,TRUE));
    assert_only_files_selected(m,0);
    guint n=g_list_model_get_n_items(G_LIST_MODEL(m));
    g_autoptr(GtkBitset) all=gtk_bitset_new_range(0,n);
    gtk_selection_model_set_selection(GTK_SELECTION_MODEL(m),all,all);assert_only_files_selected(m,4);
    gtk_selection_model_unselect_all(GTK_SELECTION_MODEL(m));
    gtk_selection_model_select_range(GTK_SELECTION_MODEL(m),0,n,TRUE);assert_only_files_selected(m,4);
}
static void test_updates_collapsed (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_autoptr(GtkTreeListRow) row=group_row(m,"application/pdf");
    gtk_tree_list_row_set_expanded(row,FALSE);
    g_auto(GStrv) collapsed_before=nautilus_view_model_dup_collapsed_groups(m);
    g_assert_true(g_strv_contains((const gchar * const *)collapsed_before,"application/pdf"));
    g_autoptr(NautilusViewItem) extra=make_item("application/pdf",99,FALSE);
    nautilus_view_model_add_item(m,extra);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,5);
    g_auto(GStrv) collapsed_after=nautilus_view_model_dup_collapsed_groups(m);
    g_assert_true(g_strv_contains((const gchar * const *)collapsed_after,"application/pdf"));
    g_autoptr(GtkTreeListRow) row2=group_row(m,"application/pdf");
    g_assert_false(gtk_tree_list_row_get_expanded(row2));
    gtk_tree_list_row_set_expanded(row2,TRUE);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,8);
    g_autoptr(NautilusViewItem) first=at(m,gtk_tree_list_row_get_position(row2)+1);
    g_assert_true(first==extra);
}
static void test_mode_switch (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));
    for(int i=0;i<10;i++)
    {
        nautilus_view_model_set_group_by_type(m,FALSE);
        g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,4);
        assert_only_files_selected(m,4);
        nautilus_view_model_set_group_by_type(m,TRUE);assert_only_files_selected(m,4);
    }
}
static void test_clear (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));
    nautilus_view_model_remove_all_items(m);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,0);assert_only_files_selected(m,0);
    g_autoptr(NautilusViewItem) item=make_item("text/plain",2,FALSE);
    nautilus_view_model_add_item(m,item);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,2);
}
static void test_mime_change (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_autoptr(GtkTreeListRow) row=group_row(m,"image/png");
    g_autoptr(NautilusViewItem) item=at(m,gtk_tree_list_row_get_position(row)+1);
    g_object_set_data_full(G_OBJECT(nautilus_view_item_get_file(item)),"mime",g_strdup("application/pdf"),g_free);
    nautilus_view_model_sort(m);
    g_assert_null(group_row(m,"image/png"));
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(m)),==,6);
}
static void test_single (void)
{
    g_autoptr(NautilusViewModel) m=sample(TRUE);
    g_assert_false(gtk_selection_model_select_item(GTK_SELECTION_MODEL(m),0,TRUE));
    gtk_selection_model_select_item(GTK_SELECTION_MODEL(m),1,TRUE);assert_only_files_selected(m,1);
    g_autoptr(GtkTreeListRow) row=group_row(m,"0:directories");gtk_tree_list_row_set_expanded(row,FALSE);
    assert_only_files_selected(m,0);
}
static gboolean no_png (gpointer value, gpointer unused)
{
    const char *mime=nautilus_file_get_mime_type(nautilus_view_item_get_file(value));
    return g_strcmp0(mime,"image/png")!=0;
}
static void test_filter (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));
    g_autoptr(GtkCustomFilter) filter=gtk_custom_filter_new(no_png,NULL,NULL);
    nautilus_view_model_set_filter(m,GTK_FILTER(filter));
    g_assert_null(group_row(m,"image/png"));assert_only_files_selected(m,3);
    nautilus_view_model_set_filter(m,NULL);
    g_autoptr(GtkTreeListRow) row=group_row(m,"image/png");g_assert_nonnull(row);assert_only_files_selected(m,3);
}
static void test_remove (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    gtk_selection_model_select_all(GTK_SELECTION_MODEL(m));
    g_autoptr(GtkTreeListRow) row=group_row(m,"image/png");
    g_autoptr(NautilusViewItem) item=at(m,gtk_tree_list_row_get_position(row)+1);
    g_autoptr(GHashTable) removal=g_hash_table_new(g_direct_hash,g_direct_equal);
    g_hash_table_insert(removal,item,nautilus_view_item_get_file(item));
    nautilus_view_model_remove_items(m,removal,NULL);
    g_assert_null(group_row(m,"image/png"));assert_only_files_selected(m,3);
}
static gboolean
strv_contains (char **values, const char *needle)
{
    if (values == NULL) return FALSE;
    for (guint i = 0; values[i] != NULL; i++)
        if (g_strcmp0 (values[i], needle) == 0) return TRUE;
    return FALSE;
}

static void test_persistent_collapsed_state (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_autoptr(GtkTreeListRow) pdf=group_row(m,"application/pdf");
    g_assert_nonnull(pdf);
    gtk_tree_list_row_set_expanded(pdf,FALSE);

    g_auto(GStrv) saved=nautilus_view_model_dup_collapsed_groups(m);
    g_assert_true(strv_contains(saved,"application/pdf"));

    nautilus_view_model_set_group_by_type(m,FALSE);
    nautilus_view_model_set_group_by_type(m,TRUE);
    g_autoptr(GtkTreeListRow) pdf2=group_row(m,"application/pdf");
    g_assert_false(gtk_tree_list_row_get_expanded(pdf2));

    const char *restore[]={"image/png",NULL};
    nautilus_view_model_set_collapsed_groups(m,restore);
    g_autoptr(GtkTreeListRow) png=group_row(m,"image/png");
    g_autoptr(GtkTreeListRow) pdf3=group_row(m,"application/pdf");
    g_assert_false(gtk_tree_list_row_get_expanded(png));
    g_assert_true(gtk_tree_list_row_get_expanded(pdf3));

    g_auto(GStrv) saved2=nautilus_view_model_dup_collapsed_groups(m);
    g_assert_true(strv_contains(saved2,"image/png"));
    g_assert_false(strv_contains(saved2,"application/pdf"));
}


static void test_group_grid_api (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    g_autoptr(GListModel) roots=nautilus_view_model_dup_group_roots_model(m);
    g_assert_nonnull(roots);
    g_assert_cmpuint(g_list_model_get_n_items(roots),==,3);

    NautilusViewItem *pdf_group=NULL;
    guint pdf_group_pos=G_MAXUINT;
    for(guint i=0;i<g_list_model_get_n_items(roots);i++)
    {
        g_autoptr(NautilusViewItem) candidate=g_list_model_get_item(roots,i);
        if(g_strcmp0(nautilus_view_item_get_group_key(candidate),"application/pdf")==0)
        {
            pdf_group=g_object_ref(candidate);
            pdf_group_pos=i;
            break;
        }
    }
    g_assert_nonnull(pdf_group);
    g_assert_cmpuint(pdf_group_pos,!=,G_MAXUINT);

    GListStore *children=nautilus_view_item_get_group_children(pdf_group);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(children)),==,2);
    g_autoptr(NautilusViewItem) first=g_list_model_get_item(G_LIST_MODEL(children),0);

    guint group_pos=G_MAXUINT, child_pos=G_MAXUINT;
    g_assert_true(nautilus_view_model_find_group_item(m,first,&group_pos,&child_pos));
    g_assert_cmpuint(group_pos,==,pdf_group_pos);
    g_assert_cmpuint(child_pos,==,0);
    g_assert_cmpuint(nautilus_view_model_get_position_for_item(m,first),!=,GTK_INVALID_LIST_POSITION);

    nautilus_view_model_set_group_collapsed(m,"application/pdf",TRUE);
    g_assert_true(nautilus_view_model_get_group_collapsed(m,"application/pdf"));
    g_assert_cmpuint(nautilus_view_model_get_position_for_item(m,first),==,GTK_INVALID_LIST_POSITION);

    nautilus_view_model_set_group_collapsed(m,"application/pdf",FALSE);
    g_assert_false(nautilus_view_model_get_group_collapsed(m,"application/pdf"));
    g_assert_cmpuint(nautilus_view_model_get_position_for_item(m,first),!=,GTK_INVALID_LIST_POSITION);

    g_object_unref(pdf_group);
}

static void test_sections_bounds (void)
{
    g_autoptr(NautilusViewModel) m=sample(FALSE);
    guint start,end,n=g_list_model_get_n_items(G_LIST_MODEL(m));
    gtk_section_model_get_section(GTK_SECTION_MODEL(m),0,&start,&end);
    g_assert_cmpuint(start,==,0);g_assert_cmpuint(end,==,n);
    gtk_section_model_get_section(GTK_SECTION_MODEL(m),n,&start,&end);
    g_assert_cmpuint(start,==,n);g_assert_cmpuint(end,==,G_MAXUINT);
    nautilus_view_model_remove_all_items(m);
    gtk_section_model_get_section(GTK_SECTION_MODEL(m),0,&start,&end);
    g_assert_cmpuint(start,==,0);g_assert_cmpuint(end,==,G_MAXUINT);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);
    parent_file=g_object_new(NAUTILUS_TYPE_FILE,NULL);
    g_test_add_func("/groups/order",test_group_sort);
    g_test_add_func("/groups/collapse-selection",test_collapse_selection);
    g_test_add_func("/groups/header-exclusion",test_no_headers);
    g_test_add_func("/groups/update-collapsed",test_updates_collapsed);
    g_test_add_func("/groups/toggle",test_mode_switch);
    g_test_add_func("/groups/clear",test_clear);
    g_test_add_func("/groups/mime-change",test_mime_change);
    g_test_add_func("/groups/single-selection",test_single);
    g_test_add_func("/groups/filter",test_filter);
    g_test_add_func("/groups/remove",test_remove);
    g_test_add_func("/groups/sections-bounds",test_sections_bounds);
    g_test_add_func("/groups/persistent-collapsed-state",test_persistent_collapsed_state);
    g_test_add_func("/groups/group-grid-api",test_group_grid_api);
    int result=g_test_run();g_object_unref(parent_file);return result;
}

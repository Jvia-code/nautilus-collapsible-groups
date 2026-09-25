#!/usr/bin/env python3
"""Static regression checks for per-directory collapse persistence and grid grouping."""
from pathlib import Path
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "src-work" / "nautilus-50.3"

def read(rel):
    p = root / rel
    if not p.is_file():
        raise SystemExit(f"ECHEC: fichier absent: {p}")
    return p.read_text(encoding="utf-8")

def require(text, needle, label):
    if needle not in text:
        raise SystemExit(f"ECHEC: {label}: motif absent: {needle}")

metadata_h = read("src/nautilus-metadata.h")
metadata_c = read("src/nautilus-metadata.c")
files_view = read("src/nautilus-files-view.c")
model_h = read("src/nautilus-view-model.h")
model_c = read("src/nautilus-view-model.c")
grid_h = read("src/nautilus-grid-view.h")
grid_c = read("src/nautilus-grid-view.c")
style_c = read("src/resources/style.css")

require(metadata_h, 'NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES', "clé de métadonnées")
require(metadata_h, 'nautilus-groupes-test-collapsed-types', "espace de noms privé")
require(metadata_c, 'NAUTILUS_METADATA_KEY_GROUP_COLLAPSED_TYPES,', "enregistrement de la métadonnée")

for needle, label in [
    ('save_group_state_for_directory (NautilusFilesView *self)', "sauvegarde par dossier"),
    ('load_group_state_for_directory (NautilusFilesView *self)', "restauration par dossier"),
    ('nautilus_file_set_metadata_list', "écriture de la liste des groupes"),
    ('nautilus_file_get_metadata_list', "lecture de la liste des groupes"),
    ('save_group_state_for_directory (self);', "appel de sauvegarde"),
    ('load_group_state_for_directory (self);', "appel de restauration"),
    ('NAUTILUS_IS_GRID_VIEW (self->list_base)', "action de regroupement disponible en grille"),
    ('nautilus_grid_view_set_group_by_type', "pilotage de la grille"),
]:
    require(files_view, needle, label)

for needle, label in [
    ('nautilus_view_model_set_collapsed_groups', "API restauration du modèle"),
    ('nautilus_view_model_dup_collapsed_groups', "API sauvegarde du modèle"),
]:
    require(model_h, needle, label)

for needle, label in [
    ('GHashTable *collapsed_groups;', "mémoire des replis"),
    ('notify::expanded', "suivi des changements d’expansion"),
    ('sync_collapsed_groups_from_rows (self);', "synchronisation avant reconstruction"),
    ('apply_collapsed_groups_to_rows (self);', "réapplication après reconstruction"),
    ('g_hash_table_contains (self->collapsed_groups, key)', "restauration de l’état"),
    ('g_sort_array (result, len, sizeof (char *), compare_strv_elements, NULL);', "tri GLib non obsolète"),
]:
    require(model_c, needle, label)

require(grid_h, 'nautilus_grid_view_set_group_by_type', "API grille")
for needle, label in [
    ('GtkBox *group_view_ui;', "conteneur vertical non virtualisé des groupes"),
    ('GtkFlowBox *flow;', "FlowBox non scrollable par groupe"),
    ('setup_cell_common_item', "cellules directes non virtualisées"),
    ('gtk_box_new (GTK_ORIENTATION_VERTICAL, 4)', "pile verticale de groupes"),
    ('create_group_block', "bloc autonome pour chaque groupe"),
    ('gtk_flow_box_bind_model', "liaison dynamique du contenu de groupe"),
    ('gtk_flow_box_set_row_spacing', "espacement compact entre rangées"),
    ('gtk_widget_set_valign (flow, GTK_ALIGN_START);', "FlowBox contracté à son contenu"),
    ('set_group_roots_model', "suivi dynamique des groupes racines"),
    ('gtk_box_append (GTK_BOX (container), header);', "en-tête au-dessus du FlowBox"),
    ('gtk_box_append (GTK_BOX (container), flow);', "FlowBox sous l’en-tête"),
    ('sync_group_flow_selection_from_model', "sélection globale synchronisée"),
    ('on_group_view_key_pressed', "Ctrl+A global dans la vue groupée"),
    ('nautilus_view_model_dup_group_roots_model', "modèle des groupes racines"),
    ('nautilus_view_model_set_group_collapsed', "repli partagé liste/grille"),
    ('nautilus_view_model_set_group_by_type (model, self->group_by_type)', "modèle groupé en grille"),
]:
    require(grid_c, needle, label)

for needle, label in [
    ('nautilus_view_model_dup_group_roots_model', "API racines de groupes"),
    ('nautilus_view_model_get_group_collapsed', "API lecture du repli"),
    ('nautilus_view_model_set_group_collapsed', "API écriture du repli"),
    ('nautilus_view_model_get_position_for_item', "mapping position globale"),
    ('nautilus_view_model_find_group_item', "mapping groupe/enfant"),
]:
    require(model_h, needle, label)


for needle, label in [
    ('.nautilus-grid-view .grouped-grid flowbox.group-flow', "CSS des FlowBox de groupe"),
    ('padding: 4px 18px 8px;', "padding vertical compact"),
    ('flowbox.group-flow > flowboxchild', "style des cellules FlowBox"),
]:
    require(style_c, needle, label)


# A NautilusGridCell runs update_icon() when its item is bound. In the grouped
# FlowBox path the icon-size binding must therefore exist first, otherwise the
# initial callback reaches gtk_icon_theme_lookup_by_gicon() with size == 0.
start = grid_c.index('create_group_flow_cell (')
end = grid_c.index('static NautilusViewItem *\ngroup_flow_child_get_item', start)
flow_cell = grid_c[start:end]
icon_bind = flow_cell.index('g_object_bind_property (data->view, "icon-size"')
item_bind = flow_cell.index('setup_cell_common_item')
if icon_bind > item_bind:
    raise SystemExit("ECHEC: la cellule FlowBox reçoit son item avant icon-size; crash GTK size == 0 possible")
require(flow_cell, 'G_BINDING_SYNC_CREATE', "initialisation immédiate de icon-size")

# The GroupRowData lifetime belongs to the outer group container. During
# container finalization GTK may already have disposed/finalized its FlowBox
# child. Explicitly calling gtk_flow_box_bind_model() from clear_group_row()
# would then dereference a stale raw pointer and emit GTK_IS_FLOW_BOX criticals.
cleanup_start = grid_c.index('clear_group_row (GroupRowData *data)')
cleanup_end = grid_c.index('static void\ngroup_row_data_free', cleanup_start)
cleanup = grid_c[cleanup_start:cleanup_end]
if 'gtk_flow_box_bind_model (' in cleanup:
    raise SystemExit("ECHEC: clear_group_row rebinde un GtkFlowBox potentiellement déjà détruit")
require(cleanup, 'GtkFlowBox owns and tears down its model binding', "documentation du cycle de vie FlowBox")

if 'gtk_stack_set_visible_child_name' in grid_c:
    raise SystemExit("ECHEC: l’ancienne tuile de groupe dans la grille plate est encore présente")
if 'GtkListView *group_view_ui;' in grid_c or 'gtk_list_view_new (NULL, factory)' in grid_c:
    raise SystemExit("ECHEC: GtkListView externe encore présent")
# GtkGridView is GtkScrollable and must remain only the flat, direct scrolled-window view.
grouped = grid_c.split('/* Grouped grid:', 1)[1].split('static GtkGridView *\ncreate_flat_view_ui', 1)[0]
if 'gtk_grid_view_new' in grouped or 'GtkGridView *grid;' in grouped:
    raise SystemExit("ECHEC: GtkGridView imbriqué dans un groupe; risque de hauteur fantôme")

print("OK: mémoire par dossier + FlowBox groupés + icon-size + cleanup sans pointeur FlowBox obsolète câblés.")

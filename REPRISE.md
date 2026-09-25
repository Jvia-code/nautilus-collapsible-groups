# REPRISE — Nautilus 50.3, groupes repliables par type

État au 25 septembre 2026 — révision **V3.3.2 Option B / FlowBox**.

## Objectif fonctionnel

Conserver Nautilus comme base, sans remplacer le Nautilus Fedora, avec :

- tri normal, par défaut nom A → Z, à l'intérieur des groupes ;
- regroupement par type MIME ;
- groupes dépliables/repliables ;
- état replié/déplié mémorisé par dossier ;
- même état entre vue liste et vue grille ;
- en vue grille, **un retour à la ligne structurel entre chaque groupe** ;
- installation privée et réversible.

## État réellement validé sur Fedora 44

La V2 a été compilée/installée et a validé le démarrage, la navigation et le regroupement en liste après correction de deux crashes réels.

La V3 initiale a été arrêtée volontairement par `/groups/update-collapsed`, révélant une perte de repli lors d'un `items-changed`. La V3.1 a corrigé ce défaut. L'utilisateur a ensuite confirmé que la V3.1 fonctionne et a fourni une capture de la grille réelle.

Cette capture a mis en évidence le défaut suivant : la V3.1 utilisait une seule `GtkGridView` plate, les en-têtes de groupe étant des tuiles ordinaires. Un nouveau groupe pouvait donc commencer dans les colonnes restantes de la même ligne.

La V3.2 a implémenté l'**Option B**, puis les tests réels Fedora ont révélé un problème de mesure de hauteur avec les sous-`GtkGridView`. La V3.3 a remplacé ces sous-grilles par des `GtkFlowBox`, mais le premier lancement réel a crashé parce que `NautilusGridCell` recevait son item avant une taille d'icône valide. La V3.3.1 corrige cet ordre d'initialisation et le lancement Fedora est visuellement concluant. Le journal révèle toutefois des `Gtk-CRITICAL` répétés dans `gtk_flow_box_bind_model()` au cleanup des groupes. La V3.3.2 supprime ce rebinding manuel sur un FlowBox potentiellement déjà détruit.

## Architecture commune du regroupement

`NautilusViewModel` conserve le modèle source et construit, lorsque le regroupement est actif, un `GtkTreeListModel`. Chaque racine est un `NautilusViewItem` de groupe avec :

- `group_key` : clé MIME ;
- `group_title` : libellé ;
- `group_children` : `GListStore` des vrais fichiers.

Les enfants restent triés par le sorter Nautilus actif. Les en-têtes ne possèdent pas de `NautilusFile` et sont exclus des opérations fichiers/sélection.

`file_group_key()` duplique le MIME emprunté de `nautilus_file_get_mime_type()` avec `g_strdup()` ; ne jamais réintroduire une propriété mémoire de type `char *` sur cette API.

## Mémoire des replis

`NautilusViewModel` maintient `collapsed_groups`, table des clés MIME repliées. Avant une reconstruction, `sync_collapsed_groups_from_rows()` capture l'état réel des racines ; après reconstruction, `apply_collapsed_groups_to_rows()` le réapplique.

`NautilusFilesView` sauvegarde la liste dans la métadonnée privée du dossier :

`nautilus-groupes-test-collapsed-types`

La sauvegarde a lieu avant le changement de dossier et lors de la destruction de la vue. La restauration a lieu au chargement du dossier.

APIs de persistance :

- `nautilus_view_model_set_collapsed_groups()` ;
- `nautilus_view_model_dup_collapsed_groups()`.

## Vue liste

`NautilusListView` reste sur l'architecture V3.1 : racines de groupes affichées avec `GtkTreeExpander`, fichiers avec les cellules Nautilus normales, en-têtes non traités comme fichiers.

## Vue grille V3.2 — Option B

### Structure

`NautilusGridView` possède désormais deux présentations :

- `view_ui` : `GtkGridView` native utilisée lorsque le regroupement est désactivé ;
- `group_view_ui` : `GtkListView` verticale utilisée lorsque le regroupement est actif.

Chaque ligne de `group_view_ui` contient un `GtkBox` vertical :

1. `GtkToggleButton` d'en-tête sur toute la largeur ;
2. une `GtkGridView` dédiée aux enfants de ce groupe ;
3. une marge basse avant le groupe suivant.

Le changement demandé par l'utilisateur est donc structurel : **les groupes ne partagent plus jamais la même grille**. Le début du groupe suivant est nécessairement sous la fin du groupe précédent.

### Sélection multi-grilles

Chaque grille de groupe reçoit un `NautilusGroupSelectionModel`, petit proxy implémentant `GListModel` + `GtkSelectionModel` :

- son `GListModel` local expose uniquement `group_children` ;
- toutes les opérations de sélection sont traduites vers le `NautilusViewModel` global ;
- Ctrl+A délègue au modèle global ;
- les signaux `selection-changed` globaux sont réémis localement ;
- les changements du modèle global forcent le rebinding des cellules locales afin de recalculer leur position globale.

Les cellules des grilles de groupe utilisent `setup_cell_common_direct()`, variante de `setup_cell_common()` adaptée à un `GtkListItem:item` qui contient directement un `NautilusViewItem`. La propriété `position` de la cellule reste une position **globale** afin que les handlers Nautilus existants de clic, contexte, activation et DnD continuent d'agir sur le modèle global.

### API ViewModel ajoutée pour la grille Option B

- `nautilus_view_model_dup_group_roots_model()` ;
- `nautilus_view_model_get_group_collapsed()` ;
- `nautilus_view_model_set_group_collapsed()` ;
- `nautilus_view_model_get_position_for_item()` ;
- `nautilus_view_model_find_group_item()`.

Le test `/groups/group-grid-api` couvre ces primitives.

## Fichiers amont modifiés

La V3.2 modifie **20 fichiers amont** :

- `data/org.freedesktop.FileManager1.service.in`
- `data/org.gnome.nautilus.gschema.xml`
- `meson.build`
- `src/nautilus-application.c`
- `src/nautilus-files-view.c`
- `src/nautilus-freedesktop-dbus.h`
- `src/nautilus-global-preferences.c`
- `src/nautilus-grid-view.c`
- `src/nautilus-grid-view.h`
- `src/nautilus-list-base-private.h`
- `src/nautilus-list-base.c`
- `src/nautilus-list-view.c`
- `src/nautilus-list-view.h`
- `src/nautilus-metadata.c`
- `src/nautilus-metadata.h`
- `src/nautilus-view-item.c`
- `src/nautilus-view-item.h`
- `src/nautilus-view-model.c`
- `src/nautilus-view-model.h`
- `src/resources/menu/nautilus-toolbar-view-menu.ui`

La différence par rapport à V3.1 est notamment l'ajout de `src/nautilus-list-base-private.h`, nécessaire à `setup_cell_common_direct()`.

## Défauts réels déjà corrigés

### Crash GSettings au démarrage

Le premier prototype relogeait illégalement le schéma fixe GTK 4 `org.gtk.gtk4.Settings.FileChooser`. GLib abortait. Le fork possède maintenant `org.gnome.Nautilus.GroupesTest.FileChooser` et reste isolé de Nautilus Fedora/GTK global.

### SIGSEGV MIME à l'ouverture d'un dossier

`nautilus_file_get_mime_type()` retourne un `const char *` emprunté. Une ancienne version le plaçait dans `g_autofree` et provoquait un `free()` invalide. `file_group_key()` retourne maintenant une copie `g_strdup()` et le double de test reproduit le vrai contrat.

### Perte du repli lors d'un `items-changed`

La V3 initiale perdait l'état lors d'une reconstruction. V3.1 synchronise l'état des `GtkTreeListRow` avant reconstruction et le réapplique ensuite. Le test `/groups/update-collapsed` protège ce scénario.

## Tests V3.2 / V3.2.2

### Réussis dans l'environnement de développement

- `python3 tests/test_mime_ownership.py src-work/nautilus-50.3` : **OK** ;
- `python3 tests/test_group_persistence_grid.py src-work/nautilus-50.3` : **OK**, vérifie maintenant explicitement l'architecture Option B (liste verticale, grille par groupe, absence de l'ancien `GtkStack` d'en-tête) ;
- `python3 tests/test_settings_isolation.py src-work/nautilus-50.3` : **OK** ;
- `python3 -m unittest -v tests/test_installer.py` : **6/6 OK** ;
- `python3 -m py_compile ...` : **OK** ;
- `bash -n installer.sh desinstaller.sh tests/run.sh` : **OK** ;
- reconstruction depuis le tarball Nautilus 50.3 vierge avec `tools/reconstruct.py` puis comparaison complète : **0 différence**.

### Tests C/GTK prévus sur Fedora

`tests/test-groups.c` contient **13 tests** :

1. `/groups/order`
2. `/groups/collapse-selection`
3. `/groups/header-exclusion`
4. `/groups/update-collapsed`
5. `/groups/toggle`
6. `/groups/clear`
7. `/groups/mime-change`
8. `/groups/single-selection`
9. `/groups/filter`
10. `/groups/remove`
11. `/groups/sections-bounds`
12. `/groups/persistent-collapsed-state`
13. `/groups/group-grid-api`

L'environnement de développement actuel ne contient pas `gtk4.pc`/les headers GTK nécessaires au build C. **Ce n'est donc pas une validation de compilation de `nautilus-grid-view.c`.** Sur Fedora 44, `installer.sh` exécute d'abord ces tests puis effectue le build Meson complet ; il n'installe rien si une étape échoue.


### Échec réel Fedora V3.2 et correctif V3.2.2

Le 25/09/2026, Fedora 44 a exécuté les **13 tests C/GTK avec succès**, ainsi que le test d’isolation GSettings. Le build Meson a ensuite échoué à la compilation de `src/nautilus-grid-view.c` avec :

```text
error: aucun prototype précédent pour « nautilus_group_selection_model_get_type » [-Werror=missing-prototypes]
```

Cause : `G_DEFINE_TYPE_WITH_CODE (NautilusGroupSelectionModel, ...)` génère une fonction globale `nautilus_group_selection_model_get_type()`. Nautilus compile avec `-Werror=missing-prototypes`; aucun prototype ne la précédait.

Correctif V3.2.2 : déclaration explicite avant le macro :

```c
static GType nautilus_group_selection_model_get_type (void);
```

Le même correctif est présent dans `tools/v32-snapshots/src/nautilus-grid-view.c`, donc `tools/reconstruct.py` le reproduit. `tests/test_group_persistence_grid.py` exige désormais ce prototype afin d’empêcher cette régression.

La V3.2 n’a pas été installée : l’échec s’est produit pendant `meson compile`, avant l’installation.

## Reproductibilité

`tools/reconstruct.py` reconstruit d'abord les modifications historiques, puis applique les snapshots V3.2 situés dans `tools/v32-snapshots/src/` pour les cinq fichiers concernés par l'Option B. Une reconstruction fraîche depuis `vendor/nautilus-50.3.tar.xz` a été comparée au `src-work` courant : **zéro différence**.

`tools/update-patch.py` régénère ensuite `nautilus-groupes.patch` et `validation/modified-files.txt`.

## Points à valider spécifiquement sur Fedora pour V3.2

1. Les 13 tests C/GTK passent.
2. Le build complet de Nautilus 50.3 passe sans erreur.
3. Chaque groupe en grille commence sur une nouvelle ligne sous le précédent.
4. Le redimensionnement redistribue les tuiles uniquement dans la grille du groupe correspondant.
5. Replier/déplier fonctionne et reste mémorisé par dossier.
6. Ctrl+A sélectionne tous les fichiers visibles du dossier, au-delà d'un seul groupe.
7. Ctrl+clic, double-clic, clic droit, renommage et DnD fonctionnent dans plusieurs groupes.
8. Navigation clavier entre groupes : vérifier notamment le passage depuis la dernière rangée d'un groupe vers le suivant.
9. Tester un dossier avec beaucoup de fichiers afin de confirmer la mesure/hauteur des grilles imbriquées.

## Procédure de checkpoint

Après toute modification importante :

1. reconstruire depuis le tarball vierge ;
2. comparer l'arbre complet ;
3. lancer `tools/update-patch.py` ;
4. exécuter les contrôles disponibles ;
5. lancer `tools/checkpoint.py <archive.zip>` ;
6. extraire le ZIP dans un dossier neuf ;
7. exécuter `python3 installer.py check` depuis cette extraction.

Ne jamais considérer une sauvegarde comme validée sur la seule présence du ZIP.

# Mise à jour V3.3 — 25/09/2026

## Retour Fedora réel sur V3.2.2

La V3.2.2 s'installe et s'exécute, mais la capture utilisateur montre que le problème de hauteur n'est **pas** corrigé : un groupe EPUB de 4 éléments affiche une seule rangée en haut de la fenêtre, puis réserve presque toute la hauteur du viewport avant le groupe JSON suivant. L'utilisateur signale également des espaces excessifs entre des rangées au sein de groupes plus fournis (PDF).

Le simple remplacement du `GtkListView` externe par un `GtkBox` n'était donc pas suffisant.

## Cause architecturale identifiée

`GtkGridView` dérive de `GtkListBase`, implémente `GtkScrollable` et est conçu pour être placé directement dans un `GtkScrolledWindow`. Dans la V3.2.2, plusieurs `GtkGridView` étaient imbriqués dans un `GtkBox`, lui-même contenu dans le `GtkScrolledWindow` principal. Leur mesure naturelle verticale pouvait alors correspondre à une configuration de très faible largeur / peu de colonnes, tandis que le rendu final utilisait de nombreuses colonnes. La grille gardait donc une allocation verticale beaucoup trop importante.

Cette architecture multi-`GtkGridView` doit être considérée comme abandonnée.

## Architecture V3.3

Le conteneur externe reste un `GtkBox` vertical. Chaque groupe contient désormais :

- un `GtkToggleButton` d'en-tête pleine largeur ;
- un `GtkFlowBox` horizontal non scrollable ;
- les mêmes `NautilusGridCell` que la vue grille native.

`GtkFlowBox` est un widget height-for-width : sa hauteur suit le nombre réel de rangées après reflow. Il ne dépend pas de la mécanique `GtkScrollable`/virtualisation de `GtkGridView`.

Le `GtkGridView` natif reste inchangé pour la vue grille **sans regroupement**.

### Nouvel helper de cellule

`setup_cell_common_item()` a été ajouté dans `nautilus-list-base.c` / `nautilus-list-base-private.h`. Il sert aux cellules non virtualisées du `GtkFlowBox` :

- affectation directe du `NautilusViewItem` ;
- affectation de la position globale ;
- installation des contrôleurs Nautilus existants (clic, contexte, DnD).

### Sélection FlowBox ↔ modèle global

Chaque bloc de groupe synchronise la sélection du `GtkFlowBox` avec le `NautilusViewModel` global :

- sélection locale → sélection globale ;
- `selection-changed` global → mise à jour des FlowBox ;
- `items-changed` global → recalcul des positions globales stockées dans les cellules ;
- Ctrl+A → sélection globale de tous les fichiers ;
- Ctrl+Maj+A → désélection globale.

Le test Fedora doit encore valider les détails UX de Ctrl+clic, Shift+clic, navigation clavier et rubberband entre plusieurs FlowBox.

## Tests V3.3 disponibles dans l'environnement de développement

- `tests/test_mime_ownership.py` : OK ;
- `tests/test_group_persistence_grid.py` : OK ;
- `tests/test_settings_isolation.py` : OK ;
- `tests/test_installer.py` : 6/6 OK ;
- `python3 -m py_compile ...` : OK ;
- `bash -n installer.sh desinstaller.sh tests/run.sh` : OK ;
- reconstruction depuis `vendor/nautilus-50.3.tar.xz` avec `tools/reconstruct.py` : **0 différence**.

Le test statique interdit désormais explicitement tout `gtk_grid_view_new()` dans la section de code de la grille groupée et exige la présence de `GtkFlowBox`.

## Limite de validation

L'environnement de développement ne possède toujours pas les headers GTK/`gtk4.pc`, donc le build C complet de cette nouvelle V3.3 n'a pas été exécuté ici. `installer.sh` sur Fedora 44 reste la validation autoritaire : il lance les 13 tests C/GTK puis le build Meson complet avant toute publication dans `~/.local/opt/nautilus-groupes-50.3`.


# Mise à jour V3.3.1 — 25/09/2026

## Crash au lancement de la V3.3

Test réel Fedora 44 : la V3.3 compilait et s'installait, mais crashait immédiatement en vue grille groupée avec :

```text
Gtk-CRITICAL: gtk_icon_theme_lookup_by_gicon: assertion 'size > 0' failed
Gtk-CRITICAL: gtk_icon_paintable_get_icon_name: assertion 'icon != NULL' failed
Erreur de segmentation
```

Cause identifiée dans `create_group_flow_cell()`: `setup_cell_common_item()` liait le `NautilusViewItem` avant que le binding `icon-size` soit créé. `NautilusGridCell` connecte son `item_signal_group` à `on_file_changed()` ; dès le bind de l'item, `update_icon()` était donc exécuté avec la valeur par défaut `icon-size == 0`.

Dans la vue grille native, la factory configure d'abord le binding `self::icon-size -> cell::icon-size` avec `G_BINDING_SYNC_CREATE`, puis l'item n'est lié que plus tard par GTK. La V3.3.1 reproduit cet ordre dans le chemin `GtkFlowBox` :

1. création de `NautilusGridCell` ;
2. binding `icon-size` avec `G_BINDING_SYNC_CREATE` ;
3. configuration des captions ;
4. seulement ensuite `setup_cell_common_item()` ;
5. contrôleurs hover / DnD.

Le test statique `tests/test_group_persistence_grid.py` vérifie désormais explicitement cet ordre et échoue si `setup_cell_common_item` repasse avant le binding `icon-size`.


# Mise à jour V3.3.2 — 25/09/2026

## Observation Fedora après V3.3.1

La vue groupée fonctionne visuellement, mais le terminal contient de nombreux :

```text
Gtk-CRITICAL: gtk_flow_box_bind_model: assertion 'GTK_IS_FLOW_BOX (box)' failed
```

Le PID concerné reste celui du Nautilus Groupes en cours d'utilisation. Le défaut provient de `clear_group_row()`: les données du bloc de groupe sont libérées depuis le conteneur parent alors que GTK a déjà pu disposer/finaliser le `GtkFlowBox` enfant. Le pointeur brut `data->flow` n'est donc plus garanti valide.

## Correctif

L'appel manuel suivant a été supprimé du cleanup :

```c
gtk_flow_box_bind_model (data->flow, NULL, NULL, NULL, NULL);
```

Il est inutile : `GtkFlowBox` démonte sa liaison au modèle lors de sa propre destruction. Les handlers connectés au modèle Nautilus sont toujours explicitement déconnectés avant de libérer `GroupRowData`.

Le test `tests/test_group_persistence_grid.py` vérifie désormais que `clear_group_row()` ne contient aucun appel à `gtk_flow_box_bind_model()`.

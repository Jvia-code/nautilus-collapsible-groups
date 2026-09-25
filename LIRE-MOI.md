# Nautilus — Groupes (prototype Fedora 44)

Ce fork privé de Nautilus 50.3 ajoute un regroupement repliable par type de fichier en **vue liste et vue grille**. Le tri choisi s'applique à l'intérieur de chaque groupe ; par défaut, la version de test démarre en vue liste, triée par nom A → Z.

L'option **« Regrouper par type »** du menu de tri active ou désactive les groupes. L'état replié/déplié est mémorisé **par dossier** et partagé entre la vue liste et la vue grille.

## V3.3.2 — grille groupée stable et cleanup FlowBox corrigé

**Correctif V3.3.2 :** le test réel Fedora de la V3.3.1 est visuellement concluant, mais le terminal révèle des `Gtk-CRITICAL` répétés (`gtk_flow_box_bind_model: GTK_IS_FLOW_BOX (box) failed`) lors de la reconstruction/destruction des groupes. Le cleanup appelait encore `gtk_flow_box_bind_model(..., NULL, ...)` via un pointeur brut vers un `GtkFlowBox` enfant qui pouvait déjà avoir été détruit. Cet appel manuel est supprimé : GTK démonte automatiquement le binding du FlowBox pendant sa destruction. Un test statique interdit désormais cet appel dans `clear_group_row()`.

La V3.2.2 a confirmé sur Fedora un problème de hauteur : les groupes étaient bien séparés, mais certaines sous-grilles réservaient une hauteur énorme. La capture réelle montrait par exemple 4 EPUB sur une seule rangée suivis de plusieurs centaines de pixels de vide avant le groupe JSON suivant.

La cause est désormais traitée à l'architecture : `GtkGridView` est un widget `GtkScrollable`, conçu pour être le contenu direct d'un `GtkScrolledWindow`. En créer une instance par groupe dans un conteneur lui-même défilant produit des mesures de hauteur inadaptées (notamment une hauteur naturelle calculée comme si la grille disposait de très peu de colonnes).

La V3.3 conserve l'Option B — **un bloc autonome par groupe** — mais remplace les sous-`GtkGridView` par des **`GtkFlowBox` non scrollables**. `GtkFlowBox` est un layout height-for-width : quand la largeur disponible augmente, il met davantage d'éléments sur une rangée et demande uniquement la hauteur nécessaire aux rangées réellement affichées.

**Correctif V3.3.1 :** la première V3.3 liait le `NautilusViewItem` à la cellule avant de lui transmettre `icon-size`. Or `NautilusGridCell` actualise immédiatement son icône lors du bind de l'item ; la taille valait donc encore 0, provoquant les assertions GTK `gtk_icon_theme_lookup_by_gicon: size > 0` puis un segfault. La V3.3.1 crée maintenant le binding `icon-size` avec `G_BINDING_SYNC_CREATE` **avant** de lier l'item, comme dans la vue grille native de Nautilus.

Structure de la grille groupée :

1. `GtkBox` vertical global dans le `GtkScrolledWindow` de Nautilus ;
2. pour chaque groupe : un en-tête pleine largeur ;
3. un `GtkFlowBox` contenant les fichiers du groupe ;
4. petit espacement puis groupe suivant.

Il n'y a donc plus aucun `GtkGridView` imbriqué dans les groupes. Le `GtkGridView` natif reste utilisé lorsque le regroupement est désactivé.

## Sélection et interactions

Les cellules `NautilusGridCell` sont conservées, avec les contrôleurs Nautilus de clic, menu contextuel et glisser-déposer. La sélection du `GtkFlowBox` est synchronisée avec le `NautilusViewModel` global. Ctrl+A et Ctrl+Maj+A sont traités au niveau de la vue groupée afin d'agir sur tous les groupes.

À valider sur Fedora : Ctrl+clic, double-clic, clic droit, glisser-déposer, sélection par rectangle et navigation clavier entre groupes.

## Installation / mise à jour

Extraire l'archive, ouvrir un terminal dans le dossier `nautilus-groupes`, puis si une version précédente est installée :

```bash
bash desinstaller.sh
bash installer.sh
```

Ne pas mettre `sudo` devant ces scripts. `installer.sh` demande lui-même sudo uniquement pour DNF et les dépendances de compilation.

Après succès :

```bash
nautilus-groupes
```

Le Nautilus Fedora n'est ni remplacé ni défini comme gestionnaire par défaut.

## Vérifications recommandées

- les **13 tests C/GTK** doivent passer avant le build ;
- vérifier que chaque groupe commence immédiatement sous le précédent ;
- vérifier un groupe sur 1 rangée puis un autre sur plusieurs rangées ;
- redimensionner fortement la fenêtre et vérifier que le FlowBox se réorganise sans créer de grand vide ;
- replier plusieurs groupes, changer de dossier, revenir et basculer liste ↔ grille ;
- tester Ctrl+A, Ctrl+clic, double-clic, clic droit, renommage et glisser-déposer ;
- tester un dossier comportant beaucoup de fichiers.

## Désinstallation

Fermer toutes les fenêtres du prototype puis :

```bash
bash desinstaller.sh
```

Les dépendances DNF et les journaux sont conservés. Les métadonnées privées `nautilus-groupes-test-*` peuvent subsister ; Nautilus Fedora ne les utilise pas.

## Diagnostic / reprise

Journal d'installation : `~/.local/state/nautilus-groupes/installation.log`.

`REPRISE.md` contient l'historique technique et les défauts corrigés. `python3 installer.py check` vérifie l'intégrité SHA-256 de l'archive sans installer.

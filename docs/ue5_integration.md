---
title: "Intégration WFC → Unreal Engine 5.7"
subtitle: "Génération de donjons 2.5D depuis le solveur C++"
date: "Mai 2026"
geometry: "margin=2cm"
---

# 1. Objectif

Ce guide explique comment utiliser le solveur WFC du dépôt parent
pour générer des donjons 3D dans Unreal Engine 5.7, en utilisant tes
propres assets (StaticMesh).

Le pipeline est en deux étapes, totalement découplées :

```
┌──────────────────────────┐    JSON     ┌─────────────────────────┐
│ wfc_dungeon (C++ binary) │ ──────────► │  WFCDungeon UE5 plugin  │
│                          │             │                         │
│ - lit un sample .txt     │             │ - parse dungeon.json    │
│ - run WFC solver         │             │ - regarde TileMapping   │
│ - écrit dungeon.json     │             │ - spawn StaticMeshes    │
└──────────────────────────┘             └─────────────────────────┘
```

Le découplage permet de regénérer le donjon hors UE5 (sans recharger
l'éditeur) et de versionner le JSON dans git ou un repo de données.

# 2. Pré-requis

- UE 5.7 (Editor + outils de compilation)
- Visual Studio 2022 (ou Rider) avec workload `Game development with C++`
- Le binaire `wfc_dungeon` du dépôt parent (`cmake --build build --target wfc_dungeon`)
- Tes assets `UStaticMesh` (sol, mur, porte) déjà importés dans UE5

# 3. Installation du plugin

## 3.1. Copie du plugin

Depuis la racine du dépôt :
```bash
# Suppose que ton projet UE5 s'appelle MyDungeonProject
cp -r ue5_plugin/WFCDungeon /chemin/vers/MyDungeonProject/Plugins/
```

Le dossier final doit ressembler à :
```
MyDungeonProject/
├── MyDungeonProject.uproject
├── Content/
├── Source/
└── Plugins/
    └── WFCDungeon/
        ├── WFCDungeon.uplugin
        └── Source/...
```

## 3.2. Régénération des fichiers de projet

- Clic droit sur `MyDungeonProject.uproject` → `Generate Visual Studio
  project files`. Les sources du plugin apparaissent désormais dans la
  solution.

## 3.3. Compilation

- Ouvre `MyDungeonProject.sln` dans Visual Studio.
- Configuration : `Development Editor / Win64`.
- Build → tu devrais voir `WFCDungeon` compiler avant ton projet.

Si ça plante avec `Cannot find module WFCDungeon`, vérifie que le
fichier `WFCDungeon.uplugin` est bien à `Plugins/WFCDungeon/`.

## 3.4. Activation dans l'éditeur

- Lance l'éditeur. Va dans `Edit → Plugins`.
- Onglet `Procedural`. Coche `WFC Dungeon`. Redémarre l'éditeur si
  demandé.

# 4. Génération du JSON côté C++

## 4.1. Choix du sample

Le dépôt parent fournit plusieurs samples. Pour un donjon classique :

```bash
cat samples/multivalue_maze.txt
```

Convention des valeurs :
- `0` = sol
- `1` = mur
- `6` = porte

Tu peux créer le tien — le format est juste une grille d'entiers,
voir `samples/multivalue_maze.txt` pour exemple.

## 4.2. Lancement du générateur

```bash
./build/wfc_dungeon samples/multivalue_maze.txt \
    --rows 32 --cols 32 \
    --seed 42 \
    --cell-size 200 --wall-height 300 \
    -o /tmp/dungeon.json
```

Sortie attendue :
```
sample=samples/multivalue_maze.txt tiles=24 success=yes collapses=467 ...
wrote /tmp/dungeon.json
```

Les paramètres clés :
- `--rows / --cols` : dimensions du donjon en cellules
- `-N` : taille de tuile WFC (2 par défaut)
- `--cell-size CM` : taille d'une cellule en cm dans UE5 (200 cm = 2 m)
- `--wall-height CM` : hauteur des murs (300 cm = 3 m)
- `--seed S` : fixé pour la reproductibilité ; un même seed → même donjon

## 4.3. Vérification rapide

```bash
python3 -c "import json; d=json.load(open('/tmp/dungeon.json')); \
            print('cells:', len(d['cells'])); \
            print('alphabet:', d['tile_alphabet'])"
```

# 5. Configuration côté UE5

## 5.1. Création du DataAsset

- Content Browser → clic droit → `Miscellaneous → Data Asset`.
- Choisis la classe `TileMappingDataAsset`. Nomme-le
  `DA_TileMapping_MyDungeon`.
- Double-clique pour l'éditer.

## 5.2. Mapping des tiles

Pour chaque `tile_id` qui apparaît dans ton JSON, ajoute une entrée
dans `TileIdToVariants`. Exemple pour un donjon classique :

| Key (tile_id) | TileName | bIsSolid | bIsDoor | FloorMesh | WallMesh | DoorMesh |
|---------------|----------|----------|---------|-----------|----------|----------|
| 0 | floor | ❌ | ❌ | `SM_Floor_Stone` | — | — |
| 1 | wall | ✅ | ❌ | — | `SM_Wall_Stone` | — |
| 6 | door | ❌ | ✅ | `SM_Floor_Stone` | — | `SM_Door_Wood` |

**Important** : `bIsSolid` doit être coché sur les tiles qui doivent
provoquer la pose d'un mur sur leurs voisins walkable. `bIsDoor`
remplace le mur par une porte.

Si certains de tes assets ont une orientation décalée (export Blender,
Maya...) — utilise `OffsetRotation` pour corriger sans toucher aux
assets eux-mêmes.

## 5.3. Placement de l'acteur

- Dans le World Outliner → `Add → Actor → All Classes → DungeonGenerator`.
- Sélectionne l'acteur. Dans Details, sous `WFC` :
  - `Json Path` : tape `/tmp/dungeon.json` (ou `Content/WFC/dungeon.json`
    si tu l'as copié dans le projet)
  - `Tile Mapping` : drag-drop ton `DA_TileMapping_MyDungeon`
  - `Cell Size Multiplier` : 1.0 par défaut
  - `Spawn Walls` : ✅
- Clique le bouton **`Generate From Json`** (visible dans Details).

Les meshes sont instanciés comme composants enfants de l'acteur. Tu
peux déplacer/tourner l'acteur, tout le donjon suit.

## 5.4. Itération

- Pour changer le donjon : relance `wfc_dungeon` avec un autre seed
  ou un autre sample. Sauvegarde le JSON. Dans UE5, sélectionne ton
  `DungeonGenerator` → `Clear Generated` → `Generate From Json`.

- Pour changer les meshes : édite ton `DA_TileMapping_*` puis
  `Clear Generated` + `Generate From Json` sur l'acteur.

# 6. Customisation avancée

## 6.1. Ajouter de nouveaux types de tile

Le JSON liste `tile_alphabet` avec un id et un nom. Si ton sample
contient une valeur que `wfc_dungeon` ne connaît pas
(`0=floor, 1=wall, 6=door`), elle sera nommée `tile_<id>`. Tu peux :

- Soit éditer `apps/wfc_dungeon.cpp` (`default_tile_name()`) pour
  ajouter des noms.
- Soit ignorer le nom (UE5 utilise l'id, pas le nom) et juste
  configurer ton DataAsset avec ce id.

## 6.2. Donjons multi-pièces avec règles spécifiques

Le solveur respecte tout sample — tu peux dessiner un sample plus
sophistiqué (corridors, salles, stairs, ...) du moment qu'il reste
2D. Plus le sample est riche, plus le donjon généré aura de variété.

## 6.3. Coins et T-junctions

La v1 traite les coins comme deux murs adjacents (pas un mesh
dédié). Si tu veux un `CornerMesh` propre, c'est marqué TODO dans
`DungeonGenerator.cpp`. Pour l'instant, en jouant sur le pivot de
ton `WallMesh` (origine au coin de la cellule), les murs adjacents
se rejoignent visuellement sans gap.

# 7. Troubleshooting

## "Could not resolve JsonPath"
Le chemin n'a pas été trouvé. Le plugin essaie dans cet ordre :
chemin tel quel (absolu), `<ProjectDir>/<Path>`, puis
`<ProjectDir>/Content/<Path>`. Vérifie que le fichier existe à au
moins l'un de ces emplacements.

## Murs orientés à l'envers
Tes assets sont exportés avec une convention différente. Édite
`OffsetRotation` dans le `FTileVariants` du tile concerné — par ex.
`Yaw=90` pour rotation de 90°. Ré-applique avec `Clear` + `Generate`.

## Floor mesh à la mauvaise hauteur
Le pivot de ton mesh n'est pas à la base. Ajuste-le dans le
StaticMesh asset (Edit → Static Mesh Editor → Build Settings) ou
applique un `OffsetRotation` qui décale en pitch.

## Échelle des cellules incorrecte
Le `cell_size_cm` dans le JSON doit correspondre à la taille réelle
de tes assets. Si ton `SM_Floor_Stone` fait 200×200 cm dans Blender,
utilise `--cell-size 200`. Sinon ajuste avec `CellSizeMultiplier`
dans l'acteur (multiplicateur, plus pratique pour itérer).

## Le donjon ne réapparaît pas après modification
Tu as oublié `Clear Generated` avant `Generate From Json`. Le
plugin n'écrase pas les composants existants.

## NavMesh ne couvre pas le donjon
Le NavMesh statique ne se met pas à jour automatiquement quand on
spawn des composants. Va dans `Build → Build Paths` (ou `Ctrl+P`).

# 8. Limitations actuelles (v1)

- Pas de multi-étages (Z toujours = 0).
- Pas de coins dédiés (le `WallMesh` est rotaté pour les coins).
- Pas de génération de NavMesh ni d'éclairage automatiques.
- Pas d'undo dans l'éditeur (utilise `Clear Generated`).
- Le plugin est marqué `Runtime` donc disponible en build packagé,
  mais l'utilité du `Generate From Json` à runtime est limitée
  (l'acteur est conçu pour la phase éditeur).

# 9. Pistes d'évolution

- Ajout d'un `CornerMesh` dédié et détection de coin convexe/concave.
- Support des escaliers (multi-étages) via une grille 3D.
- Choix random du variant parmi plusieurs `WallMesh` (mur cassé, mur
  ornementé, ...) pour casser la répétition visuelle.
- Bake automatique du NavMesh post-`Generate`.
- Un nouveau `EditorUtilityWidget` pour éviter d'avoir à drag-drop
  l'acteur — bouton "Generate Dungeon" depuis un panneau custom.

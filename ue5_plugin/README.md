# WFCDungeon — plugin Unreal Engine 5.7

Plugin C++ qui matérialise un donjon 2.5D dans UE5 à partir d'un JSON
produit par le binaire `wfc_dungeon` du dépôt parent.

## Installation

1. Copie le dossier `WFCDungeon/` dans le répertoire `Plugins/` de ton projet UE5 :
   ```
   MonProjet/
   └── Plugins/
       └── WFCDungeon/
           ├── WFCDungeon.uplugin
           └── Source/...
   ```
   Crée le dossier `Plugins/` s'il n'existe pas encore.

2. Ferme l'éditeur UE5 s'il est ouvert.

3. Clique droit sur `MonProjet.uproject` → `Generate Visual Studio project files`.

4. Ouvre `MonProjet.sln` dans Visual Studio (ou Rider). Compile la
   configuration `Development Editor / Win64`. Les sources du plugin
   apparaissent dans `Plugins/WFCDungeon`.

5. Relance l'éditeur. Le plugin doit apparaître dans
   `Edit → Plugins → Procedural → WFC Dungeon`. Vérifie qu'il est activé.

## Workflow rapide

1. Côté C++ (depuis le dépôt parent), génère un JSON :
   ```bash
   ./build/wfc_dungeon samples/multivalue_maze.txt \
       --rows 32 --cols 32 --seed 42 \
       --cell-size 200 --wall-height 300 \
       -o /tmp/dungeon.json
   ```
2. Copie `dungeon.json` quelque part d'accessible à UE5
   (par ex. `MonProjet/Content/WFC/dungeon.json` — UE peut lire le JSON
   en relatif au répertoire du projet).

3. Dans UE5 :
   - Crée un `UTileMappingDataAsset` :
     `Add → Miscellaneous → Data Asset → TileMappingDataAsset`.
   - Pour chaque `tile id` qui apparaît dans ton JSON, crée une entrée
     dans `TileIdToVariants` :
     - `0` → walkable, `FloorMesh` = sol, `bIsSolid = false`
     - `1` → solid, `WallMesh` = mur, `bIsSolid = true`
     - `6` → door, `bIsDoor = true`, `DoorMesh` = porte
   - Drag `ADungeonGenerator` dans la scène.
   - Dans Details : assigne `JsonPath` (chemin vers `dungeon.json`),
     `TileMapping` (ton DataAsset).
   - Clique le bouton **`Generate From Json`** sous "WFC". Les meshes
     s'instancient comme composants enfants de l'acteur.

4. Pour rejouer avec un autre JSON ou ajuster les rotations :
   - `Clear Generated` → `Generate From Json`.

## Convention de coordonnées

- Le JSON utilise `(r, c)` = (ligne, colonne) en row-major.
- Le plugin pose le mesh à `(c × CellCm, r × CellCm, 0)` dans l'espace
  local de l'acteur. **L'axe X = colonnes (droite)**, **Y = lignes (avant)**.
- Les murs sont rotatés en yaw (rotation autour de Z) :
  - N → −90°
  - S → +90°
  - E → 0°
  - W → 180°

Si tes assets sont orientés selon une autre convention (ex. exportés
depuis Blender avec +Y forward), utilise le champ `OffsetRotation`
de chaque `FTileVariants` pour appliquer une correction systématique.

## Champs `FTileVariants`

| Champ | Type | Rôle |
|-------|------|------|
| `TileName` | `FString` | nom lisible (debug uniquement) |
| `bIsSolid` | `bool` | tile bloquant (déclenche les murs adjacents) |
| `bIsDoor` | `bool` | tile porte (remplace mur par porte sur les voisins walkable) |
| `FloorMesh` | `UStaticMesh*` | sol pour les tiles walkable |
| `WallMesh` | `UStaticMesh*` | mur posé entre walkable et solid |
| `CornerMesh` | `UStaticMesh*` | (réservé pour v2 — coins de pièce) |
| `DoorMesh` | `UStaticMesh*` | porte qui remplace le mur côté door |
| `OffsetRotation` | `FRotator` | yaw/pitch/roll de correction d'orientation |
| `ScaleOverride` | `FVector` | scale par variant (asset à mauvaise échelle) |

## Limitations connues (v1)

- Pas de génération multi-étages (tout est sur Z=0).
- Pas de placement automatique d'enemies, loots ou lights.
- Pas de NavMesh rebuild — clique `Build → Build Paths` après génération.
- Les coins de pièce sont actuellement représentés par un `WallMesh`
  rotaté ; un `CornerMesh` dédié sera ajouté dans une future version.
- Pas d'undo dans l'éditeur — utilise `Clear Generated` puis
  `Generate From Json` pour relancer.

## Compatibilité UE

- Cible : UE 5.7
- Devrait compiler sans modification depuis UE 5.2 (utilise uniquement
  des APIs `UPROPERTY`/`UFUNCTION CallInEditor`/`UDataAsset` qui n'ont
  pas changé sur cette gamme de versions).
- Les `TObjectPtr<>` exigent UE 5.0+.

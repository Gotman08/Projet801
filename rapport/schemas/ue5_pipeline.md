# Schéma : Pipeline UE5 wfc_dungeon → JSON → spawn meshes

## Référencé dans
- `chapters/extensions_algorithmiques.tex`, figure `fig:ue5_pipeline`

## Cible
- Fichier : `results/figures/schemas/ue5_pipeline.png`
- Format : 1600×600, paysage
- Style : pipeline horizontal avec icônes simples

## Prompt ChatGPT

```
A clean horizontal pipeline diagram with four steps connected by arrows.

STEP 1 (left): Box labeled "wfc_dungeon CLI". Below: small icon of a
terminal window. Caption: "input: sample.txt".

ARROW right.

STEP 2: Box labeled "dungeon.json". Below: icon of a JSON file with
brackets. Caption: "metadata + grid + cells + neighbors + stairs".

ARROW right.

STEP 3: Box labeled "UE5 Plugin (WFCDungeon)". Below: small Unreal
Engine 5 logo or "UE5" text. Caption: "ADungeonGenerator AActor +
TileMappingDataAsset".

ARROW right.

STEP 4 (right): Box labeled "Spawned 3D scene". Below: icon of a small
3D dungeon (cubes, walls). Caption: "StaticMeshComponent per cell
with cardinal yaw".

White background, clean borders, minimal text. Use the burgundy color
(#8e5e2d) for accents. Steps numbered 1-2-3-4 above the boxes.
```

## Notes
- L'objectif : illustrer la chaîne complète CLI -> JSON -> plugin UE5
  -> scène 3D.
- Il n'est pas nécessaire d'avoir un logo UE5 exact, "UE5" en text
  stylisé suffit.

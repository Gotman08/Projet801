# Schéma : Pipeline UE5 wfc_dungeon → JSON → spawn meshes

## Référencé dans
- `chapters/extensions_algorithmiques.tex`, figure `fig:ue5_pipeline`

## Cible
- Fichier : `results/figures/schemas/ue5_pipeline.png`
- Format : 1600×600, paysage
- Style : pipeline horizontal avec icônes simples

## Note importante (correction par rapport à v1)

La première version mentionnait « stairs » dans la box JSON. Le commit
`5f61397` (refactor wfc_dungeon : drop multi-floor support) a retiré le
support multi-étages. Le pipeline est maintenant strictement single-
floor. Le nouveau prompt ci-dessous reflète ce changement et inclut les
nouvelles features de gameplay (PlayerStart, NPC spawners, pickups,
NavMeshBoundsVolume, mesh variants).

## Prompt ChatGPT

```
A clean horizontal pipeline diagram with four steps connected by arrows.

STEP 1 (left): Box labeled "wfc_dungeon CLI". Below: small icon of a
terminal window. Caption: "input: sample.txt + connectivity check".

ARROW right.

STEP 2: Box labeled "dungeon.json". Below: icon of a JSON file with
brackets. Caption: "metadata + grid + tile_alphabet + cells (single
floor, with cardinal neighbors per cell)".

ARROW right.

STEP 3: Box labeled "UE5 Plugin (WFCDungeon)". Below: small "UE5" text
inside a rounded rectangle (no real Unreal logo, just a stylized
placeholder). Caption: "ADungeonGenerator AActor + TileMappingDataAsset
(mesh variants per tile id)".

ARROW right.

STEP 4 (right): Box labeled "Spawned 3D scene + gameplay". Below: icon
of a small isometric dungeon (a few cubes for walls, a flat floor,
plus a small character/NPC sphere and a coin/pickup icon). Caption:
"StaticMesh per cell + NavMeshBoundsVolume + PlayerStart + NPC
spawners + random pickups (no overlap)".

White background, clean borders, minimal text. Use a burgundy color
(approx #8e5e2d) for accents. Steps numbered 1-2-3-4 above the boxes.

NO mentions of stairs or multi-floor. Single-floor pipeline only.
```

## Notes
- L'objectif : illustrer la chaîne complète CLI -> JSON -> plugin UE5
  -> scène 3D, en reflétant les features actuelles (gameplay actors,
  nav mesh, mesh variants).
- Si ChatGPT ajoute des étages, lui rappeler explicitement « single
  floor only, no stairs ».
- Pas besoin du logo UE5 exact, juste « UE5 » stylisé pour identifier
  le moteur.

# Schéma : Compatibilité d'overlap entre deux tuiles

## Référencé dans
- `chapters/approche_algorithmique_sequentielle.tex`, figure `fig:overlap_compatibility`

## Cible
- Fichier : `results/figures/schemas/overlap_compatibility.png`
- Format : 1200×800, paysage
- Style : schéma technique géométrique, axes annotés

## Prompt ChatGPT

```
A clean technical diagram showing two 2x2 tiles partially overlapping
on a coordinate grid.

Setup:
- A coordinate system with x-axis pointing right and y-axis pointing
  down (computer graphics convention), labeled "x" and "y" with arrows.
- Tile t1 (blue outline) at position (0,0), occupying cells (0,0),
  (1,0), (0,1), (1,1). Inside the cells, draw small numbers like
  "a b / c d" representing values.
- Tile t2 (red outline) shifted by offset (dx=1, dy=0), occupying
  cells (1,0), (2,0), (1,1), (2,1). Inside, draw small numbers.
- The overlapping region (cells (1,0) and (1,1), shared between t1
  and t2) is highlighted in light yellow.
- Below the diagram, equation: "compatibility: t1[y][x] = t2[y][x-1]
  for all (x,y) in overlap region".

Use thin black grid lines, blue and red colored outlines for tiles,
yellow fill for overlap. White background. Clean and minimal.
```

## Notes
- L'objectif pédagogique : montrer visuellement ce que signifie
  "compatibilité à offset (dx, dy)".
- Garder le schéma simple, deux tuiles 2x2 avec un offset (1, 0) suffit.

# Schéma : Compatibilité d'overlap entre deux tuiles

## Référencé dans
- `chapters/approche_algorithmique_sequentielle.tex`, figure `fig:overlap_compatibility`

## Cible
- Fichier : `results/figures/schemas/overlap_compatibility.png`
- Format : 1200×800, paysage
- Style : schéma technique géométrique, axes annotés

## Note importante (correction par rapport à v1)

Une première version dessinait t1 et t2 **côte à côte** (adjacentes, donc
sans recouvrement). C'est faux pour offset (1, 0) avec des tuiles 2×2 :
les deux tuiles doivent **se chevaucher** d'1 colonne. Le nouveau prompt
ci-dessous insiste explicitement sur le chevauchement spatial.

## Prompt ChatGPT

```
A clean technical diagram showing two 2x2 tiles that PHYSICALLY OVERLAP
on a coordinate grid. The overlap is the central feature of the
illustration.

Setup:
- Coordinate axes: x pointing right and y pointing down (computer
  graphics convention), labels "x" and "y" with arrows.
- A grid of 4 columns wide and 2 rows tall, cells of equal size.
- Tile t1 (BLUE outline) covers cells at columns 0-1 (x-range [0, 2])
  and rows 0-1.
- Tile t2 (RED outline) covers cells at columns 1-2 (x-range [1, 3])
  and rows 0-1, so t2 is shifted by exactly ONE cell to the right
  compared to t1.
- The shared region (column 1, both rows) is HIGHLIGHTED in YELLOW.
  This shared region must be visibly INSIDE BOTH the blue and red
  outlines simultaneously, showing they truly overlap, not just sit
  next to each other.
- The two yellow cells contain values that BELONG TO BOTH tiles. To
  emphasize this, write each yellow cell with a label like
  "t1[0][1] = t2[0][0]" (top yellow) and "t1[1][1] = t2[1][0]"
  (bottom yellow).
- Outside the overlap: cell (0, 0) of t1 labeled "a", cell (0, 1)
  labeled "c"; cell (2, 0) of t2 labeled "f", cell (2, 1) labeled "h".
- Annotation pointing to t1: "t1, top-left at (0,0)".
- Annotation pointing to t2: "t2, top-left at (1,0), dx=1, dy=0".
- Below the diagram, equation:
  "compatibility: t1[y][x] = t2[y][x-dx] for all (x, y) in overlap".

Use thin black grid lines, thick blue and red outlines for tiles,
yellow fill for overlap. White background. Clean and minimal. The
visual must make it obvious that t1 and t2 share two physical cells.
```

## Notes
- Le piège à éviter : ChatGPT a tendance à dessiner les tuiles comme
  deux rectangles séparés qui se touchent. Le prompt insiste plusieurs
  fois sur le chevauchement (« physically overlap », « inside both
  outlines », « truly overlap, not just sit next to each other »).
- Si la première sortie est encore fausse, ajouter dans le prompt :
  "the blue and red outlines must visibly cross each other in the
  middle column".

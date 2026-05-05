# Schéma : Layout mémoire de la Wave (buffer plat)

## Référencé dans
- `chapters/implementation_cpp_ingenierie_logicielle.tex`, figure `fig:wave_layout`

## Cible
- Fichier : `results/figures/schemas/wave_memory_layout.png`
- Format : 1400×800, paysage
- Style : diagramme mémoire technique

## Prompt ChatGPT

```
A clean technical diagram showing memory layout of a flat 2D wave
storage with bitset views.

LEFT SIDE: a 4x4 grid representing rows x cols of cells. Each cell
labeled (r,c) like (0,0), (0,1), ..., (3,3). Above the grid:
"Logical Wave: rows x cols cells".

ARROW pointing down with label "flat storage".

CENTER: a long horizontal rectangle representing one contiguous
std::vector<u64> buffer. Divided into 16 sections, labeled
"(0,0) | (0,1) | (0,2) | (0,3) | (1,0) | ..." up to "(3,3)". Below:
"std::vector<u64>, size = rows * cols * words_per_cell, single
allocation".

RIGHT SIDE: zoom on cell (1, 2) showing it points to a specific
slice of the buffer. Label "BitsetView: pointer + size, no allocation".

Use light grey for the buffer, blue accent for the highlighted cell.
Arrows in dark grey. White background. NO redundant text. Clean,
technical diagram style like a Computer Architecture textbook.
```

## Notes
- L'objectif : illustrer pourquoi notre Wave est plus efficace qu'un
  vector<Bitset> (une seule allocation, layout cache-friendly).
- Important : l'arrow du cell logique vers le slice du buffer doit être
  visible.

# Schéma : Camembert de profilage du WFC séquentiel

## Référencé dans
- `chapters/resultats_et_analyse.tex`, figure `fig:profile_breakdown`

## Cible
- Fichier : `results/figures/schemas/profile_breakdown.png`
- Format : 1200×900 (carré ou portrait)
- Style : camembert clair, palette professionnelle

## Prompt ChatGPT

```
A clean pie chart showing time breakdown of a WFC algorithm execution
on a 128x128 grid with L=11 tiles.

Five slices:
1. "Min-entropy selection" - 85% (largest slice, color: red/orange)
2. "BFS propagation" - 12% (medium slice, color: blue)
3. "RNG / weighted_pick" - 3% (small slice, color: grey)
4. "Tile extraction" - <0.01% (tiny sliver, color: light green)
5. "Adjacency rules build" - <0.01% (tiny sliver, color: light green)

Each slice has a percentage label outside the pie.

Title above: "Profiling WFC serial 128x128, L=11"
Subtitle: "Total: ~3.97 seconds on Romeo EPYC 9654"

Style: clean professional pie chart, like a scientific paper figure.
White background, distinct slice colors with no gradients. Bold
percentages. The 85% slice should clearly dominate visually.
```

## Notes
- L'objectif : montrer visuellement que la sélection min-entropie domine
  largement.
- Les deux mini-slices (extraction + règles) sont à peine visibles, c'est
  voulu.

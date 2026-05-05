# Schéma : Pipeline sample → solveur → sortie

## Référencé dans
- `chapters/probleme_wfc.tex`, figure `fig:sample_to_output`

## Cible
- Fichier : `results/figures/schemas/sample_to_output.png`
- Format : 1400×600, paysage
- Style : pipeline horizontal, technique

## Prompt ChatGPT

```
A clean horizontal pipeline diagram with three boxes connected by arrows.

LEFT BOX: a 5x5 pixel grid showing the WFC paper's binary sample
(values: row 1 = 1 0 1 1 1, row 2 = 1 0 1 1 1, row 3 = 0 0 1 1 1,
row 4 = 0 1 1 1 1, row 5 = 0 0 0 0 0). Black squares for 0, white for 1.
Label below: "Sample S, 5x5".

ARROW pointing right with text "WFC solver" above it.

RIGHT BOX: a 64x64 pixel grid showing a procedurally-generated binary
output that locally resembles the sample (similar diagonal patterns,
rectangular regions). Label below: "Output G, 64x64".

NO OTHER TEXT. Clean white background. Black borders on the boxes.
Sharp pixel grid lines visible.
```

## Notes
- Le sample doit être strictement le sample du sujet (5x5 binaire avec
  les valeurs spécifiques).
- L'output peut être stylisé / illustratif, n'a pas besoin d'être un
  vrai output WFC.

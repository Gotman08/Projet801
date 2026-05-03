# WFC benchmark — diagnostic Romeo

## Sommaire
- Runs totaux : **36**
- Échecs (`success=0`) : **0**
- Configurations agrégées : **18**

## Variance
Toutes les configs ont CV ≤ 10 % — bonne reproductibilité.

## Label : `smoke`

### Sweet spot par taille

Le "sweet spot" est le nombre de threads qui maximise le speedup
(t ≥ 2 ; t=1 OMP est exclu — il représente l'overhead de la version
parallèle sans bénéfice).

| Size | Threads sweet | Speedup | Efficacité | Solve (s) |
|------|---------------|---------|------------|-----------|
| 32×32 | 4 | 1.08× | 27 % | 0.014 |
| 64×64 | 4 | 2.54× | 63 % | 0.099 |
| 128×128 | 4 | 3.34× | 83 % | 1.172 |

### Speedup maximum par taille

| Size | Threads | Speedup max | Solve (s) | Efficacité |
|------|---------|-------------|-----------|------------|
| 32×32 | 4 | 1.08× | 0.014 | 27 % |
| 64×64 | 4 | 2.54× | 0.099 | 63 % |
| 128×128 | 4 | 3.34× | 1.172 | 83 % |

### Loi d'Amdahl — fraction parallélisable estimée

| Size | Speedup max | Threads | Fraction parallélisable estimée |
|------|-------------|---------|--------------------------------|
| 32×32 | 1.08× | 4 | 9.8 % |
| 64×64 | 2.54× | 4 | 80.7 % |
| 128×128 | 3.34× | 4 | 93.4 % |

## Sanity check — propagations / collapses

Le ratio propagations/collapses indique la "profondeur" moyenne du BFS
après chaque collapse. Il devrait être stable pour une même config
(seed différent → ordre différent mais ordre de grandeur conservé).

| Label | Size | Ratio prop/collapse (median) | min | max |
|-------|------|------------------------------|-----|-----|
| smoke | 32×32 | 8.0 | 7.9 | 8.0 |
| smoke | 64×64 | 7.7 | 7.6 | 7.7 |
| smoke | 128×128 | 7.7 | 7.6 | 7.8 |


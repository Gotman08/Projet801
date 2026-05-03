# WFC benchmark — diagnostic Romeo

## Sommaire
- Runs totaux : **220**
- Échecs (`success=0`) : **0**
- Configurations agrégées : **44**

## Variance
Toutes les configs ont CV ≤ 10 % — bonne reproductibilité.

## Label : `binary_L11`

### Sweet spot par taille

Le "sweet spot" est le nombre de threads qui maximise le speedup
(t ≥ 2 ; t=1 OMP est exclu — il représente l'overhead de la version
parallèle sans bénéfice).

| Size | Threads sweet | Speedup | Efficacité | Solve (s) |
|------|---------------|---------|------------|-----------|
| 32×32 | 4 | 1.16× | 29 % | 0.013 |
| 64×64 | 4 | 2.53× | 63 % | 0.098 |
| 128×128 | 8 | 5.11× | 64 % | 0.777 |
| 256×256 | 16 | 7.42× | 46 % | 8.420 |

### Speedup maximum par taille

| Size | Threads | Speedup max | Solve (s) | Efficacité |
|------|---------|-------------|-----------|------------|
| 32×32 | 4 | 1.16× | 0.013 | 29 % |
| 64×64 | 4 | 2.53× | 0.098 | 63 % |
| 128×128 | 8 | 5.11× | 0.777 | 64 % |
| 256×256 | 16 | 7.42× | 8.420 | 46 % |

### Effet NUMA (96 → 192 threads, traverse les sockets)

| Size | Speedup @ 96 | Speedup @ 192 | Gain double-socket |
|------|--------------|---------------|--------------------|
| 32×32 | 0.05× | 0.02× | ×0.40 ⚠️ |
| 64×64 | 0.15× | 0.06× | ×0.39 ⚠️ |
| 128×128 | 0.23× | 0.10× | ×0.45 ⚠️ |
| 256×256 | 0.56× | 0.17× | ×0.31 ⚠️ |

### Loi d'Amdahl — fraction parallélisable estimée

| Size | Speedup max | Threads | Fraction parallélisable estimée |
|------|-------------|---------|--------------------------------|
| 32×32 | 1.16× | 4 | 18.9 % |
| 64×64 | 2.53× | 4 | 80.6 % |
| 128×128 | 5.11× | 8 | 91.9 % |
| 256×256 | 7.42× | 16 | 92.3 % |

## Sanity check — propagations / collapses

Le ratio propagations/collapses indique la "profondeur" moyenne du BFS
après chaque collapse. Il devrait être stable pour une même config
(seed différent → ordre différent mais ordre de grandeur conservé).

| Label | Size | Ratio prop/collapse (median) | min | max |
|-------|------|------------------------------|-----|-----|
| binary_L11 | 32×32 | 7.7 | 7.5 | 8.0 |
| binary_L11 | 64×64 | 7.7 | 7.6 | 7.9 |
| binary_L11 | 128×128 | 7.7 | 7.6 | 7.8 |
| binary_L11 | 256×256 | 7.8 | 7.7 | 7.8 |
| binary_L11 | 512×512 | 7.8 | 7.6 | 7.8 |


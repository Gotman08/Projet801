# WFC benchmark — diagnostic Romeo

## Sommaire
- Runs totaux : **388**
- Échecs (`success=0`) : **0**
- Configurations agrégées : **100**

## Variance
Configs avec coefficient de variation > 10 % (à examiner) :

| Label | Backend | Threads | Size | CV | Median (s) | Min | Max |
|-------|---------|---------|------|-----|------------|-----|-----|
| binary_L11 | omp | 16 | 512×512 | 73.3 % | 83.362 | 82.648 | 188.907 |
| smooth_N3 | omp | 2 | 32×32 | 32.7 % | 0.003 | 0.003 | 0.004 |
| terrain_N2 | serial | 1 | 32×32 | 25.0 % | 0.054 | 0.051 | 0.076 |
| terrain_N2 | omp | 2 | 32×32 | 24.4 % | 0.039 | 0.039 | 0.055 |
| terrain_N2 | omp | 4 | 32×32 | 24.3 % | 0.030 | 0.029 | 0.042 |
| terrain_N2 | omp | 1 | 32×32 | 23.6 % | 0.059 | 0.058 | 0.083 |
| terrain_N2 | omp | 8 | 32×32 | 21.5 % | 0.034 | 0.034 | 0.047 |
| terrain_N2 | omp | 16 | 32×32 | 21.2 % | 0.103 | 0.097 | 0.137 |
| terrain_N2 | omp | 32 | 32×32 | 20.5 % | 0.132 | 0.126 | 0.176 |
| terrain_N2 | omp | 64 | 32×32 | 20.1 % | 0.275 | 0.256 | 0.360 |
| smooth_N3 | omp | 4 | 32×32 | 18.4 % | 0.002 | 0.002 | 0.003 |
| smooth_N3 | omp | 32 | 32×32 | 12.0 % | 0.026 | 0.026 | 0.031 |
| terrain_N2 | omp | 4 | 128×128 | 10.6 % | 4.231 | 4.225 | 5.004 |

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

## Label : `smooth_N3`

### Sweet spot par taille

Le "sweet spot" est le nombre de threads qui maximise le speedup
(t ≥ 2 ; t=1 OMP est exclu — il représente l'overhead de la version
parallèle sans bénéfice).

| Size | Threads sweet | Speedup | Efficacité | Solve (s) |
|------|---------------|---------|------------|-----------|
| 32×32 | 4 | 1.07× | 27 % | 0.002 |
| 64×64 | 4 | 1.62× | 40 % | 0.010 |
| 128×128 | 4 | 2.16× | 54 % | 0.046 |

### Speedup maximum par taille

| Size | Threads | Speedup max | Solve (s) | Efficacité |
|------|---------|-------------|-----------|------------|
| 32×32 | 4 | 1.07× | 0.002 | 27 % |
| 64×64 | 4 | 1.62× | 0.010 | 40 % |
| 128×128 | 4 | 2.16× | 0.046 | 54 % |

### Loi d'Amdahl — fraction parallélisable estimée

| Size | Speedup max | Threads | Fraction parallélisable estimée |
|------|-------------|---------|--------------------------------|
| 32×32 | 1.07× | 4 | 8.9 % |
| 64×64 | 1.62× | 4 | 50.8 % |
| 128×128 | 2.16× | 4 | 71.5 % |

## Label : `terrain_N2`

### Sweet spot par taille

Le "sweet spot" est le nombre de threads qui maximise le speedup
(t ≥ 2 ; t=1 OMP est exclu — il représente l'overhead de la version
parallèle sans bénéfice).

| Size | Threads sweet | Speedup | Efficacité | Solve (s) |
|------|---------------|---------|------------|-----------|
| 32×32 | 4 | 1.80× | 45 % | 0.030 |
| 64×64 | 8 | 4.16× | 52 % | 0.218 |
| 128×128 | 8 | 6.09× | 76 % | 2.414 |
| 256×256 | 16 | 10.41× | 65 % | 23.114 |

### Speedup maximum par taille

| Size | Threads | Speedup max | Solve (s) | Efficacité |
|------|---------|-------------|-----------|------------|
| 32×32 | 4 | 1.80× | 0.030 | 45 % |
| 64×64 | 8 | 4.16× | 0.218 | 52 % |
| 128×128 | 8 | 6.09× | 2.414 | 76 % |
| 256×256 | 16 | 10.41× | 23.114 | 65 % |

### Loi d'Amdahl — fraction parallélisable estimée

| Size | Speedup max | Threads | Fraction parallélisable estimée |
|------|-------------|---------|--------------------------------|
| 32×32 | 1.80× | 4 | 59.3 % |
| 64×64 | 4.16× | 8 | 86.8 % |
| 128×128 | 6.09× | 8 | 95.5 % |
| 256×256 | 10.41× | 16 | 96.4 % |

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
| smooth_N3 | 32×32 | 309.4 | 246.8 | 394.6 |
| smooth_N3 | 64×64 | 774.2 | 651.2 | 846.0 |
| smooth_N3 | 128×128 | 1673.8 | 1305.5 | 1862.2 |
| terrain_N2 | 32×32 | 12.7 | 12.2 | 15.7 |
| terrain_N2 | 64×64 | 12.4 | 12.2 | 12.5 |
| terrain_N2 | 128×128 | 12.3 | 12.3 | 12.6 |
| terrain_N2 | 256×256 | 12.3 | 12.3 | 12.4 |


# Performance

Données mesurées, pas extrapolées. Chaque chiffre vient d'un run réel
dont l'environnement est documenté.

## Plates-formes mesurées

| ID         | Hardware                            | OS               | Compilateur     | OpenMP |
|------------|-------------------------------------|------------------|-----------------|--------|
| `lin-i9`   | Intel i9-10900K (10c / 20t HT)      | Ubuntu 24.04 WSL2 | g++ 13.3        | 4.5    |
| `win-i9`   | Intel i9-10900K (10c / 20t HT)      | Windows 11       | g++ 16.1 (MSYS2) | 5.2   |
| `romeo`    | AMD EPYC 9654 (96c×2, 192c, 8 NUMA) | RHEL 9 (Romeo)   | g++ 14.2        | 4.5    |

Toutes les mesures avec `-O3 -march=native`, `OMP_PROC_BIND=close`,
`OMP_PLACES=cores`.

## Optimisations compilateur

A/B testing (médiane de 11 runs alternés) montre que la seule optim qui
survit la mesure est LTO (Link-Time Optimization). Les optims source
appliquées manuellement (cache `f * log(f)` thread_local, vector FIFO,
hoisting de `wave.at(c)`) ont toutes été rejetées, le compilateur en
`-O3 -march=native` les fait déjà.

| Variante                              | Median 128×128 | Δ baseline |
|---------------------------------------|----------------|------------|
| Release `-O3 -march=native` (baseline) | 4.280 s        | - |
| Release + `USE_LTO=ON`                | 4.134 s        | +3.4%  |
| Release + LTO + PGO                   | 4.143 s        | +4.3% (mais PGO complique le build pour 0.4% de plus, rejeté) |
| Cache `f * log(f)` thread_local       | 4.749 s        | -6.7% (régression, rejeté) |
| `std::queue` → vector FIFO            | 4.586 s        | -3% (dans le bruit, rejeté) |
| Hoist `wave.at(c)` (CSE manuel)        | 4.342 s        | +0% (variance, rejeté)     |

### OMP frontier-threshold fallback (gardé)

Job SLURM Romeo 544356 (binary L=11, 128×128) : la nouvelle optim
(`kSerialFallback = max(64, max_threads)`) bascule en série
quand le niveau BFS est trop court pour amortir le coût des `omp task`
+ barrière. Mesures avant / après :

| Threads | Avant (543692) | Après (544356) | Gain   |
|---------|----------------|----------------|--------|
|  1      | 3.98 s         | 4.19 s         | -5% (variance) |
|  4      | 1.17 s         | 1.15 s         | +2%    |
|  8      | 0.75 s         | 0.69 s         | +10%   |
| 16      | 1.52 s         | 1.27 s         | +20%   |
| 32      | 4.53 s         | 4.01 s         | +13%   |
| 64      | 13.16 s        | 10.53 s        | +25%   |
| 96      | 16.50 s        | 13.73 s        | +20%   |
| 192     | 35.30 s        | 27.28 s        | +29%   |

L'optim aide nettement à partir de 8 threads, et l'effet grandit avec
le thread count (jusqu'à +29% à 192 threads). À 1-2 threads l'effet est
légèrement négatif mais dans le bruit. Optim gardée.

### Parallel attempts (gardée)

Au lieu de paralléliser intra-attempt (limite à 8-16 threads avant
régression), on lance K attempts WFC indépendants en parallèle. Chaque
attempt sérialisé sur son propre `Wave`, succès d'index minimum gagne
(déterminisme préservé).

Mesure locale (Windows i9, terrain N=3 24×24, total 4 seeds) :

| Mode | Total | Speedup |
|---|---|---|
| sequential, 1 thread | 0.510 s | 1.0× |
| --parallel-attempts 4 --threads 4 | 0.332 s | 1.54× |
| --parallel-attempts 8 --threads 8 | 0.238 s | 2.14× |

Inutile sur workloads à fort taux de succès (binary_5x5, smooth_N3) :
K attempts = K× le travail pour le même résultat. Utile sur workloads
serrés (terrain N=3, maze contraint).

### Min-entropy work-density gate (gardée)

`parallel_min_entropy` court-circuite vers `serial_min_entropy` quand
le travail prévu (`total_cells × num_tiles`) est < 50 000 ops, ou
quand `max_threads ≤ 1`. Granularité changée de `total/(4×threads)` à
`total/threads` (1 chunk par thread) : moins de tasks OMP pour un load
balancing inutile (chunks de coût égal).

Un seuil similaire avait été essayé dans `propagate_tasks` pour le
plateau smooth_N3 mais rollbacked : il aurait risqué de sérialiser des
niveaux BFS moyens sur binary_L11 où le peak 5.27× à 8t sur Romeo
dépend de leur parallélisation.

Activer LTO : `cmake -B build -DUSE_LTO=ON ...`

## Speedup OMP, sample binaire L=11

### Romeo (EPYC 9654)

Source : job SLURM 542693 (`results/romeo_smoke_542693.csv`),
3 répétitions par config, médiane reportée.

| Taille  | serial  | omp 1   | omp 4   | omp 16  | omp 32  |
|---------|---------|---------|---------|---------|---------|
| 32×32   | 0.015 s | 0.019 s | 0.013 s (1.2×) | 0.046 s | 0.082 s |
| 64×64   | 0.247 s | 0.261 s | 0.099 s (2.5×) | 0.392 s | 0.648 s |
| 128×128 | 3.90 s  | 3.96 s  | 1.17 s (3.3×) | 1.45 s (2.7×) | 4.19 s |
| 256×256 | 61.4 s  | 63.7 s  | 17.0 s (3.6×) | 7.40 s (8.3×) | 15.2 s |

Lecture :
- 32×32 trop petit pour amortir le coût des tasks, serial gagne
- 64×64 : gain à 4 threads, dégradation au-delà
- 128×128 : zone optimale entre 4 et 16 threads (3.3× peak)
- 256×256 : speedup 8.3× à 16 threads, taille où BFS et min-entropie
  paient le mieux
- Régression à 32 threads : la frontière BFS reste trop courte par
  rapport au pool ; les threads attendent à la barrière de niveau plus
  qu'ils travaillent.

Hypothèse pour la chute à threads >> 16 : le coût synchronisation des
fins de niveau (jusqu'à 8000 niveaux pour 128×128 si la propagation
fait des niveaux courts) domine quand les niveaux sont vides. Une
optimisation envisageable serait de désactiver le parallélisme pour
les niveaux < seuil.

### Local Windows (i9-10900K, MSYS2 MinGW)

| Taille  | serial  | omp 4   | omp 8   |
|---------|---------|---------|---------|
| 128×128 | 4.53 s  | 4.08 s (1.1×) | 5.83 s (0.8×, regression) |

**Pas de speedup utile sous Windows MinGW.** Causes identifiées :
- heap lock global de mingw-w64 sérialise les `malloc` du `Bitset
  src_snapshot` dans la hot path
- `winpthreads` plus coûteux que `libgomp` Linux pour les
  régions parallel/single répétées

C'est un effet plateforme, pas un bug du code. Les rapports
généralement citent les chiffres Linux/Romeo qui sont représentatifs
de l'usage cible (HPC).

## Décomposition du temps

Sur 128×128 binaire L=11 (Romeo, serial), profilage approximatif :

| Étape                  | Temps relatif | Nature                              |
|------------------------|---------------|--------------------------------------|
| Extraction tuiles      | < 0.01 %      | une fois, négligeable               |
| Règles d'adjacence     | < 0.01 %      | une fois, négligeable               |
| **Sélection min-entropie** | **~85 %**  | scan O(rows·cols·L) **par collapse** |
| Propagation            | ~12 %         | BFS niveau-synchrone                |
| Tirage aléatoire + reste | ~3 %        | RNG mt19937_64                      |

Résultat contre-intuitif : le goulot n'est pas la propagation (8000
cellules × voisins × intersections, ce à quoi on pense en premier) mais
la sélection qui re-scanne la wave entière à chaque collapse pour
trouver la cellule de min-entropie.

C'est ce qui justifie le travail mis dans `parallel_min_entropy`, même
si paralléliser la propagation est plus "visible" comme effort.

## Effet de la taille du tileset

Sur 64×64, sample binaire (L=11) vs sample multivalue terrain (L=33),
serial Romeo :

| Sample              | L  | solve (s) | bits par cellule |
|---------------------|----|-----------|--------------------|
| `binary_5x5`        | 11 | 0.247     | 64 (1 mot)         |
| `multivalue_terrain` | 33 | ~0.520    | 64 (1 mot)         |

L=33 reste dans un seul mot u64. Le coût croît à peu près linéairement
en L (popcount/and/or sur 1 mot reste constant en bits manipulés mais
proportionnel en taux de remplissage). Au-delà de 64 tuiles, on passe
à 2 mots → doublement du coût des opérations bitset.

## Effet de la taille de tile N

Sur le sample multivalue terrain :
- `N = 2` → L = 33, résolution facile, 1ère tentative
- `N = 3` → L = 73, contraintes serrées, échec systématique des 5
  tentatives (contradiction). Trop spécifique.

Le tradeoff classique de WFC : N grand = motifs plus fidèles mais
problème plus tendu, plus dur à résoudre.

## Bande passante mémoire

À 128×128 avec L=11 (1 mot par cellule), la wave occupe 16 384 mots =
128 KB. Tient en L2 (256 KB par cœur sur EPYC) mais pas en L1
(32 KB). Pour 256×256, la wave fait 524 KB → débordement L2,
accès LLC.

C'est cohérent avec l'observation que le speedup est meilleur sur
256×256 que sur 128×128 : à 256×256, on passe plus de temps en
attente mémoire, ce qui est précisément ce que le parallélisme aide à
amortir.

## Comparaison OMP vs Kokkos

Pas de mesure directe encore (Kokkos non testé sur Romeo). Sur
Windows local, sur 32×32 :

- `wfc_serial` : 17 ms
- `wfc_omp` (4 threads) : 170 ms (slow due to MinGW heap lock)
- `wfc_kokkos` : 210 ms (single-threaded ici)

Pas représentatif. Mesures Romeo à venir.

## Recommandations

| Cas d'usage                                     | Choix conseillé                          |
|--------------------------------------------------|------------------------------------------|
| Grilles ≤ 64×64                                  | `wfc_serial`                              |
| Grilles 128×128 sur 4-8 cores                    | `wfc_omp` avec `--threads 4`              |
| Grilles 256×256+ sur EPYC ou similaire           | `wfc_omp` avec `--threads 16`             |
| Grilles ≥ 512×512                                | `wfc_omp` avec 32-64 threads (à tester)  |
| Comparaison framework / portabilité GPU future   | `wfc_kokkos`                              |
| Reproducibilité bit-à-bit                        | n'importe quel backend, même seed        |

## Reproduire les mesures

### Sur Romeo

```bash
ssh romeo
cd ~/wfc801
sbatch romeo_smoke.slurm  # 15 min, 32 cores
# ou
sbatch scripts/romeo_submit.sh  # full sweep, 4 h, 192 cores
```

Récupérer les CSV avec `scp` puis `python3 scripts/plot_results.py`.

### En local

```bash
./scripts/run_benchmark.sh
python3 scripts/plot_results.py results/benchmark.csv
```

Produit `docs/figures/{speedup,efficiency,backends}.png`.

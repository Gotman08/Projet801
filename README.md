# Wave Function Collapse : Projet 801

Implémentation C++17 du *Wave Function Collapse overlapping model* (WFC), avec
trois backends : série, OpenMP (tâches explicites), Kokkos. Le sujet complet
est dans [`README.pdf`](README.pdf).

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) : modules et leurs dépendances
- [docs/ALGORITHM.md](docs/ALGORITHM.md) : algorithme WFC tel qu'implémenté
- [docs/CHOICES.md](docs/CHOICES.md) : décisions techniques et raisons
- [docs/BUILD.md](docs/BUILD.md) : build sous Linux / Windows / Romeo + Kokkos
- [docs/TESTING.md](docs/TESTING.md) : couverture des tests
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) : analyse perf avec données Romeo
- [docs/results.md](docs/results.md) : galerie d'images générées
- [docs/benchmark.md](docs/benchmark.md) : analyse de scaling SLURM
- [docs/report.md](docs/report.md) : rapport académique
- [docs/slides.md](docs/slides.md) : slides de présentation
- [docs/ue5_integration.md](docs/ue5_integration.md) : démo Unreal Engine 5 (optionnelle, voir `BUILD_DUNGEON`)

## Ce que ça fait

Lit une grille échantillon, en extrait toutes les tuiles `N × N`, calcule
leurs règles d'adjacence, puis génère une nouvelle grille (taille libre)
qui ne contient localement que des tuiles vues dans l'échantillon.

Backends :
- `wfc_serial` : référence séquentielle
- `wfc_omp` : parallélisé avec `#pragma omp task` (sélection min-entropie + propagation BFS)
- `wfc_kokkos` : variante Kokkos (`parallel_for` + atomics) pour comparaison

Les trois produisent un output bit-identique pour un même seed.

## Build

Pré-requis : un compilateur C++17 avec OpenMP, CMake ≥ 3.16.
Plateformes vérifiées :
- **Linux** : g++ 13.3 sur Ubuntu 24.04 / WSL2, gcc 14.2 sur Romeo (RHEL 9, AMD EPYC 9654 192 cores).
- **Windows natif** : MSYS2 + MinGW-w64 UCRT (g++ 16.1, OpenMP 5.2, ninja).

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
cmake --build build -j
```

Pour activer Kokkos en plus (testé avec Kokkos 4.4.01, backends OPENMP+SERIAL) :

```bash
./scripts/build_kokkos.sh   # télécharge et installe Kokkos dans external/
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON -DUSE_KOKKOS=ON \
      -DKokkos_ROOT=$PWD/external/kokkos/install
cmake --build build -j
```

## Démo Unreal Engine 5 (optionnel)

Cible CMake additionnelle `wfc_dungeon` qui génère un JSON pour un
plugin UE 5.7 (`ue5_plugin/WFCDungeon/`). Ne touche ni au benchmark ni
aux solveurs parallèles : utilise uniquement `WFCSolverSerial` via
l'interface publique. Activer avec `-DBUILD_DUNGEON=ON` :

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON -DBUILD_DUNGEON=ON
cmake --build build -j
./build/wfc_dungeon samples/multivalue_maze.txt --rows 24 --cols 24 \
    -N 2 --seed 42 --levels 3 -o dungeon.json
```

Le pipeline UE5 (asset → JSON → spawn de meshes) est documenté dans
[docs/ue5_integration.md](docs/ue5_integration.md). Sans
`-DBUILD_DUNGEON=ON`, la cible n'est pas générée et le build reste
identique au pipeline HPC.

Aucun impact perf : la cible `wfc_dungeon` n'est pas liée au
`wfc_benchmark`, aux suites de tests, ni aux solveurs parallèles.
Vérifié localement (binary 64×64 best-of-3) avec et sans
`-DBUILD_DUNGEON=ON` : mêmes temps serial / omp1 / omp4 / omp8 dans
le bruit de mesure.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

10 suites cœur (`test_bitset`, `test_grid`, `test_grid_io`,
`test_tileset`, `test_overlap`, `test_wave`, `test_solver_common`,
`test_solver`, `test_edge_cases`, `test_parallel_attempts`) plus trois
conditionnels (`test_solver_omp`, `test_solver_kokkos`,
`test_kokkos_autoinit`) qui vérifient le déterminisme bit-à-bit serial
vs backend parallèle pour {1, 2, 4, 8} threads, et le succès
d'index minimum en mode parallel-attempts.

## Usage

### Solveur série

```bash
./build/wfc_serial samples/binary_5x5.txt --rows 64 --cols 64 -N 2 \
    --seed 42 --out result.txt --png result.png --scale 6
```

### Solveur OpenMP

```bash
OMP_NUM_THREADS=8 ./build/wfc_omp samples/binary_5x5.txt --rows 128 --cols 128 \
    -N 2 --seed 42 --threads 8
```

### Solveur Kokkos

```bash
./build/wfc_kokkos samples/binary_5x5.txt --rows 128 --cols 128 -N 2 \
    --seed 42 --kokkos-num-threads=8
```

### Options communes (`--help` pour la liste complète)

| Option            | Description                                  |
|-------------------|----------------------------------------------|
| `--rows`, `--cols` | dimensions de la grille de sortie            |
| `-N`              | taille de tuile (défaut 2)                   |
| `--seed`          | seed RNG (output déterministe pour un seed donné) |
| `--attempts`      | nombre maximal de retentatives sur contradiction |
| `--parallel-attempts K` | lance K attempts en parallèle, garde le succès d'index minimum (défaut 1) |
| `--symmetries S`  | expansion D4 du tile set : 1, 2, 4, 8 (défaut 1, désactivé) |
| `--backtrack`     | utilise le backtracking au lieu du restart sur contradiction (défaut désactivé) |
| `--threads`       | threads (OMP)                                |
| `--scale`         | facteur de zoom du rendu PPM/PNG             |
| `--out FILE.txt`  | écriture de la grille texte                  |
| `--ppm FILE.ppm`  | rendu PPM (P6)                               |
| `--png FILE.png`  | rendu PNG via `stb_image_write.h`            |

### Options optionnelles, zéro impact sur la perf si désactivées

`--parallel-attempts` paie sur les workloads serrés où chaque attempt a
un risque d'échec (ex. terrain N=3) : 2.14× wallclock observé à K=8 vs
K=1 sur terrain N=3 24×24. Inutile sur les workloads qui réussissent
toujours du premier coup : K attempts = K× le travail pour le même
résultat.

`--symmetries S` étend le catalogue de tuiles avec les variantes D4
(rotations 90°/180°/270° et leurs réflexions horizontales). Les
variantes héritent de la fréquence de leur source. À S=1 (défaut), le
chemin est strictement identique au comportement legacy : aucune
génération de variant, aucun coût additionnel. À S>1, le seul coût est
une étape one-shot lors de l'extraction (quelques µs même pour gros
samples). Effet sur le solver : `L` croît jusqu'à 8× → bitsets passent
parfois à 2 mots → solver ~1.5× plus lent. Bénéfice : motifs
asymétriques (chemins, branchages, escaliers) appliqués uniformément
dans toutes les orientations.

`--backtrack` remplace la stratégie restart-on-contradiction par un
parcours arborescent : chaque collapse pousse une frame
(cellule, choix restants, snapshot wave) sur une pile ; en cas de
contradiction la frame du sommet est dépilée et le choix suivant est
essayé. Utile sur les samples très contraints où retry échoue
systématiquement (ex. terrain N=3 32×32 : retry échoue en 30
attempts, backtrack résout en ~80 ms). Default désactivé : le chemin
hot reste inchangé. Coût mémoire : `O(rows·cols·words_per_cell)` par
frame de pile, soit ~32 KB pour 64×64 binaire ; OK jusqu'à 256×256
mais attention au-delà.

## Format des échantillons

Texte simple, espaces ou retours à la ligne entre valeurs, lignes commençant
par `#` ignorées. Toutes les lignes doivent avoir la même largeur.

```
# 5x5 binary sample
1 0 1 1 1
1 0 1 1 1
0 0 1 1 1
0 1 1 1 1
0 0 0 0 0
```

Échantillons fournis : `samples/binary_5x5.txt` (exemple du sujet),
`binary_stripes`, `binary_checker`, `binary_dots`, `multivalue_terrain`,
`multivalue_maze`, `multivalue_smooth` (3 valeurs, transitions douces).

## Benchmarks

```bash
./scripts/run_benchmark.sh           # build/wfc_benchmark + sweep
python3 scripts/plot_results.py results/benchmark.csv
                                     # produit docs/figures/{speedup,efficiency,backends}.png
```

Le sweep par défaut couvre 32×32, 64×64, 128×128 × {1, 2, 4, 8} threads × {serial, omp, kokkos}.

### Sur Romeo (HPC, AMD EPYC 9654 192c, NVIDIA GH200)

```bash
sbatch scripts/romeo_full_bench.slurm   # CPU full sweep ~1h
sbatch scripts/build_kokkos_gpu_romeo.slurm  # GPU build + tests ~15 min
sbatch scripts/romeo_gpu_bench.slurm    # GPU bench ~10 min
```

Mesures combinées (jobs 543692 + 544061 + 544356) sur `binary_5x5` :

| Taille  | serial  | omp peak       | omp threads peak | régression 192t |
|---------|---------|----------------|------------------|-----------------|
| 64×64   | 0.25 s  | 0.094 s (2.6×) | 8 threads        | 3.93 s (15× plus lent) |
| 128×128 | 3.97 s  | 0.69 s (5.7×)  | 8 (avec optim)   | 27.3 s (6.9× plus lent) |
| 256×256 | 61.4 s  | 7.5 s (8.2×)   | 16 threads       | 319 s (5× plus lent) |

L'optim "frontier threshold"
([WFCSolverOMP.cpp:188](src/solvers/WFCSolverOMP.cpp#L188)) bascule en
série pour les niveaux BFS courts. Gain mesuré : +10% à 8 threads,
+25% à 64 threads, +29% à 192 threads.

GPU GH200 testé sur `binary_5x5` 128×128 : 5.4 s, 8× plus lent que
OMP CPU 8 threads. Les H↔D copies par propagate (~16 GB pour 256×256)
dominent le coût. Voir [docs/benchmark.md](docs/benchmark.md) pour
l'analyse complète.

## Rapport et présentation

```bash
pandoc docs/report.md  -o docs/report.pdf  --toc --pdf-engine=xelatex
pandoc -t beamer docs/slides.md -o docs/slides.pdf
```

## Layout

```
include/wfc/   headers publics (Grid, Tile, Bitset, TileSet, OverlapRules,
               Wave, WFCSolver, GridIO + solvers/)
src/           implémentations
apps/          wfc_serial, wfc_omp, wfc_kokkos, benchmark
tests/         test_grid, test_tileset, test_overlap, test_solver
samples/       grilles d'entrée
scripts/       run_benchmark.sh, plot_results.py, build_kokkos.sh
results/       CSV des benchmarks
docs/          report.md, slides.md, figures/
third_party/   stb_image_write.h
```

## Licence

Projet académique. Code original sous MIT, `stb_image_write.h` sous Public
Domain (cf. en-tête du fichier).

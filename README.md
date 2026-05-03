# Wave Function Collapse — Projet 801

Implémentation C++17 du *Wave Function Collapse overlapping model* (WFC), avec
trois backends : série, OpenMP (tâches explicites), Kokkos. Le sujet complet
est dans [`README.pdf`](README.pdf).

## Ce que ça fait

Lit une grille échantillon, en extrait toutes les tuiles `N × N`, calcule
leurs règles d'adjacence, puis génère une nouvelle grille (taille libre) qui
ne contient localement que des tuiles vues dans l'échantillon.

**Backends :**
- `wfc_serial` — implémentation de référence séquentielle.
- `wfc_omp` — parallélisé avec `#pragma omp task` (sélection min-entropie + propagation BFS).
- `wfc_kokkos` — variante Kokkos (`parallel_for` + atomics) pour comparaison.

Les trois produisent **un output bit-identique** pour un même seed.

## Build

Pré-requis : un compilateur C++17 avec OpenMP (testé avec g++ 13.3 sur
Ubuntu 24.04 / WSL2), CMake ≥ 3.16.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
cmake --build build -j
```

Pour activer Kokkos en plus :

```bash
./scripts/build_kokkos.sh   # télécharge et installe Kokkos dans external/
cmake -B build -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON -DUSE_KOKKOS=ON \
      -DKokkos_ROOT=$PWD/external/kokkos/install
cmake --build build -j
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

Quatre suites : grille, extraction de tuiles, règles d'adjacence (avec
symétrie), solveur (déterminisme + soundness).

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
| `--attempts`      | nombre de retentatives sur contradiction     |
| `--threads`       | threads (OMP)                                |
| `--scale`         | facteur de zoom du rendu PPM/PNG             |
| `--out FILE.txt`  | écriture de la grille texte                  |
| `--ppm FILE.ppm`  | rendu PPM (P6)                               |
| `--png FILE.png`  | rendu PNG via `stb_image_write.h`            |

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
`multivalue_maze`.

## Benchmarks

```bash
./scripts/run_benchmark.sh           # build/wfc_benchmark + sweep
python3 scripts/plot_results.py results/benchmark.csv
                                     # produit docs/figures/{speedup,efficiency,backends}.png
```

Le sweep par défaut couvre 32×32, 64×64, 128×128 × {1, 2, 4, 8} threads × {serial, omp, kokkos}.

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

Projet académique — code original sous MIT, `stb_image_write.h` sous Public
Domain (cf. en-tête du fichier).

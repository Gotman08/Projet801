# Architecture

Vue d'ensemble du code. Une seule lecture suffit pour savoir où ajouter
quoi sans lire tous les fichiers.

## Couches

```
┌──────────────────────────────────────────────────────────────────────┐
│   apps/                                                              │
│   wfc_serial    wfc_omp    wfc_kokkos    wfc_benchmark               │
│       └────────────┬────────────┘             └─ sweeps & CSV       │
│                    │                                                  │
│                    ▼ (dépend du backend choisi)                       │
│   src/solvers/                                                        │
│   WFCSolverSerial    WFCSolverOMP    WFCSolverKokkos                 │
│       └────────────┬────────────┘                                     │
│                    │ hérite de                                        │
│                    ▼                                                  │
│   src/internal/                                                       │
│   WFCSolverBase   ←  boucle solve() : pick → collapse → propagate    │
│   SolverCommon    ←  weighted_entropy, weighted_pick, jitter, ...    │
│                    │ utilise                                          │
│                    ▼                                                  │
│   src/   include/wfc/                                                 │
│   Grid     Tile     Bitset     TileSet     OverlapRules     Wave    │
│   GridIO   (read/write txt/ppm/png)                                   │
└──────────────────────────────────────────────────────────────────────┘
```

Trois bibliothèques CMake distinctes, chacune avec ses dépendances :

| Cible CMake          | Sources                                              | Dépend de                                  |
|----------------------|------------------------------------------------------|--------------------------------------------|
| `wfc_core`           | `Grid`, `TileSet`, `OverlapRules`, `Wave`, `GridIO`, `SolverCommon`, `WFCSolverBase`, `WFCSolverSerial` | `OpenMP::OpenMP_CXX` si `USE_OMP=ON` (juste pour `OverlapRules::build` parallèle) |
| `wfc_omp_lib`        | `WFCSolverOMP`                                       | `wfc_core`, `OpenMP::OpenMP_CXX`           |
| `wfc_kokkos_lib`     | `WFCSolverKokkos`                                    | `wfc_core`, `Kokkos::kokkos`               |

Les exécutables tirent uniquement les libs dont ils ont besoin. `wfc_serial`
ne dépend ni d'OpenMP ni de Kokkos ; `wfc_benchmark` les ajoute conditionnellement
selon `WFC_HAS_OMP` / `WFC_HAS_KOKKOS`.

## Modules — qui possède quoi

### `Grid` ([include/wfc/Grid.hpp](../include/wfc/Grid.hpp))
La grille d'entrée et de sortie. Stockage `std::vector<std::uint8_t>`
row-major. Accesseurs : `at(r, c)`, `at_torus(r, c)` (wrap toroïdal).
Header-only à part le fichier `.cpp` symbolique.

### `Tile` ([include/wfc/Tile.hpp](../include/wfc/Tile.hpp))
Pattern N×N. POD avec `std::vector<Value> data` + `int N`. Hashable par
FNV-1a (pour la déduplication dans `TileSet`).

### `Bitset` ([include/wfc/Bitset.hpp](../include/wfc/Bitset.hpp))
Bitset packé sur `std::uint64_t`. Trois classes :
- `Bitset` — propriétaire (`std::vector<u64>`)
- `BitsetView` — vue mutable non-propriétaire (utilisée pour les cellules de `Wave`)
- `ConstBitsetView` — vue lecture-seule

Toutes les opérations vectorielles (`and_with`, `or_with`, `count`,
`first_set`, `for_each_set`) traitent 64 bits à la fois et utilisent les
intrinsics `__builtin_popcountll` / `__builtin_ctzll` (avec fallback MSVC
`__popcnt64` / `_BitScanForward64`).

### `TileSet` ([include/wfc/TileSet.hpp](../include/wfc/TileSet.hpp))
Construit par `TileSet::from_sample(grid, N)`. Extraction toroïdale :
chaque position `(r0, c0)` génère un tile en lisant `N×N` valeurs avec
wrap-around. Déduplication via `std::unordered_map<Tile, int, TileHash>`.

### `OverlapRules` ([include/wfc/OverlapRules.hpp](../include/wfc/OverlapRules.hpp))
Table de compatibilité `tile × offset → bitset_de_tiles`. Pour des tiles
N×N, les offsets pertinents sont `(dx, dy) ∈ [-(N-1), N-1]²`, soit
`(2N-1)²` offsets. Stockée à plat : indexation `t1 * offsets + offset_index(dx, dy)`.

Construction : pour chaque paire `(t1, t2)` et chaque `(dx, dy)`, on
vérifie que les valeurs sur la zone de recouvrement coïncident. La
boucle externe `for t1` est parallélisée par `#pragma omp parallel for`
quand `WFC_CORE_HAS_OMP` est défini (~2× sur 8 cores pour gros tilesets).

### `Wave` ([include/wfc/Wave.hpp](../include/wfc/Wave.hpp))
État de superposition de la grille de sortie. Pour chaque cellule, un
bitset de tile-ids encore possibles. **Stockage** : un seul `std::vector<u64>`
contigu pour toutes les cellules, indexé par `cell_id × words_per_cell`.

Les accesseurs `at(r, c)` retournent une `BitsetView` qui ne référence
qu'une tranche de ce buffer ; jamais d'allocation par cellule.

Le constructeur fait un *first-touch parallèle* (pragma omp parallel for
sur l'init) pour que chaque thread mappe les pages physiques sur son
NUMA node — important sur EPYC avec 8 NUMA nodes.

### `WFCSolverBase` ([include/wfc/internal/WFCSolverBase.hpp](../include/wfc/internal/WFCSolverBase.hpp))
Squelette commun. Boucle `run_attempt` :
```
loop:
   cell = pick_cell()        ← virtuel
   if cell < 0: success
   tile = weighted_pick(cell)
   wave[cell].set_only(tile)
   if !propagate(cell): contradiction → fail attempt
```
La méthode publique `solve()` enchaîne `max_attempts` tentatives avec
des seeds dérivés (`attempt_seed`).

Les solveurs concrets surchargent `pick_cell` et `propagate`. Tout le
reste (init wave, tirage pondéré, mesure de stats, build_output) est
factorisé.

### `SolverCommon` ([include/wfc/internal/SolverCommon.hpp](../include/wfc/internal/SolverCommon.hpp))
Helpers purs partagés entre tous les backends :
- `weighted_entropy(wave, freq)` — entropie de Shannon pondérée
- `weighted_pick(wave, freq, rng)` — tirage par fréquence (CDF cumulative)
- `cell_jitter(cell, seed)` — bruit déterministe pour départager les égalités
- `attempt_seed(base, n)` — dérive le seed de la n-ième tentative
- `serial_min_entropy(wave, freq, seed)` — scan séquentiel (utilisé par Serial et Kokkos)
- `build_output(wave, tiles)` — reconstruit la grille finale

### `WFCSolverSerial` ([src/solvers/WFCSolverSerial.cpp](../src/solvers/WFCSolverSerial.cpp))
Référence séquentielle. `pick_cell` = `serial_min_entropy`, propagation
BFS classique avec `std::queue`. ~75 lignes.

### `WFCSolverOMP` ([src/solvers/WFCSolverOMP.cpp](../src/solvers/WFCSolverOMP.cpp))
Backend OpenMP avec `#pragma omp task` explicite (exigé par le sujet).
Deux étapes parallélisées :

1. **Min-entropie** (`parallel_min_entropy`) — chunked + réduction ordonnée
   pour rester déterministe.
2. **Propagation BFS** (`propagate_tasks`) — région `parallel` ouverte UNE
   FOIS pour toute la propagation ; le `single` pilote les niveaux. Tasks
   par chunk de cellules sur chaque niveau. Atomic word-level
   `__atomic_fetch_and` pour les écritures concurrentes sur la wave.

Optimisations spécifiques :
- `OffsetScratch` aligné `alignas(64)` pour éviter le faux partage
- snapshot du bitset de la cellule avant traitement (évite les races avec
  les écritures concurrentes des voisins)
- skip relaxed-load avant `lock and` (réduit ~50% du trafic atomique)
- per-thread frontier buffer (`ThreadFrontier`), agrégé en fin de niveau

### `WFCSolverKokkos` ([src/solvers/WFCSolverKokkos.cpp](../src/solvers/WFCSolverKokkos.cpp))
Variante Kokkos. `Kokkos::parallel_for` sur la frontière BFS,
`Kokkos::atomic_fetch_and` pour les écritures, même structure que OMP
mais sans tasks (Kokkos privilégie le data-parallelism).

## Conventions de code

- C++17, sans dépendance externe runtime à part `stb_image_write.h` (en
  `third_party/`, single-header public domain).
- `Value = std::uint8_t` partout (alphabet jusqu'à 256 valeurs).
- Headers publics dans `include/wfc/`, sources dans `src/`. Headers
  internes (helpers SolverCommon, base de classes) dans `include/wfc/internal/`.
- Aucun warning sous `-Wall -Wextra -Wpedantic -Werror` (g++ 14+, clang).
- Pas de `std::cout`/`printf` dans `src/` : I/O réservée aux apps.

## Ajouter un nouveau backend

1. Créer `include/wfc/solvers/WFCSolverFoo.hpp` qui hérite de `WFCSolverBase`
   et déclare `pick_cell`, `propagate`, `backend_name() = "foo"`.
2. Créer `src/solvers/WFCSolverFoo.cpp` avec l'implémentation.
3. Ajouter une cible CMake `wfc_foo_lib` dans `CMakeLists.txt`.
4. Ajouter `apps/wfc_foo.cpp` (4 lignes : parse + run).
5. Ajouter `tests/test_solver_foo.cpp` (vérifie `serial == foo` bit-à-bit).

Aucune modification du cœur (Grid/TileSet/Wave/...) n'est nécessaire
pour ajouter un nouveau backend.

## Ajouter une fonctionnalité

| Type d'ajout                          | Fichiers à toucher                              |
|---------------------------------------|--------------------------------------------------|
| Nouveau format d'image                | `GridIO.{hpp,cpp}` + test_grid_io                |
| Nouvelle stratégie de sélection       | `WFCSolverBase` (changer `pick_cell` virtuelle) ou un solveur concret |
| Symétries (rotations/reflexions)      | `TileSet::from_sample` (étendre la génération) + test_tileset |
| Nouveau format d'entrée               | `GridIO::read_grid_*` + test                     |
| Nouvelle métrique d'entropie          | `SolverCommon::weighted_entropy`                 |
| Nouvelle option CLI                   | `apps/cli_common.hpp::Args` + `parse`            |
| Nouveau benchmark                     | `apps/benchmark.cpp` (parse_args, run_one) + script slurm si HPC |

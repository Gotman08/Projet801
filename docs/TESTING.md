# Stratégie de tests

Liste explicite des invariants vérifiés et de ce qui n'est pas couvert.

## Lancement

```bash
ctest --test-dir build --output-on-failure
```

Sans Kokkos : 13 suites (`test_bitset`, `test_grid`, `test_grid_io`,
`test_tileset`, `test_overlap`, `test_wave`, `test_solver_common`,
`test_solver`, `test_edge_cases`, `test_parallel_attempts`,
`test_symmetries`, `test_backtrack`, `test_solver_omp`).
Avec `-DUSE_KOKKOS=ON` : 15 suites (ajoute `test_solver_kokkos` et
`test_kokkos_autoinit`).

Chaque suite est un binaire indépendant ; chacun rapporte
`X/Y checks passed` à la fin et exit non-nul en cas d'échec.

## Chiffres actuels (build complet, OMP + Kokkos)

| Suite                   | Checks  | Rôle principal                                  |
|-------------------------|---------|--------------------------------------------------|
| `test_bitset`           |    310  | bitset packé : set/clear/and/or/popcount/multi-mots |
| `test_grid`             |    778  | Grid : accès, torus, fill_row_major, edge cases  |
| `test_grid_io`          |     70  | round-trip txt, PNG IHDR width/height parsing, PPM header parsing, parsing erreurs |
| `test_tileset`          |    132  | extraction, fréquences, déduplication            |
| `test_overlap`          |  4 898  | identité, symétrie, multivalue, cas dégénérés   |
| `test_wave`             |  8 594  | superposition, accesseurs, indépendance          |
| `test_solver_common`    |  9 060  | entropy, weighted_pick (incl. zero-weight), jitter, build_output deterministic |
| `test_solver`           |     71  | end-to-end serial : déterminisme + soundness     |
| `test_edge_cases`       |    404  | failure paths, scale<1, validate, garbage parsing, stb png failure, retry exhaustion |
| `test_parallel_attempts` |    16  | parallel_attempts validation, déterminisme lowest-success, retry boost terrain N=3, parité OMP/serial à K>1 |
| `test_symmetries`       |     66  | rotated_90⁴ = id, reflected² = id, S=1 = legacy bit-à-bit, monotonic growth S=2/4/8, dédup auto-symétriques, validation S invalides |
| `test_backtrack`        |    520  | use_backtracking=false unchanged, soundness, succès sur sample tight où retry-1 échoue, déterminisme |
| `test_solver_omp`       |     36  | déterminisme OMP avec success checks bilatéraux, contradiction, multivalue |
| `test_solver_kokkos`    |     22  | déterminisme Kokkos avec success checks bilatéraux, contradiction, multivalue |
| `test_kokkos_autoinit`  |      3  | chemin auto-init/finalize de Kokkos             |
| TOTAL                   | 24 980  | |

## Couverture (gcovr 8.6, build_cov)

```bash
cmake -B build_cov -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSE_OMP=ON -DUSE_KOKKOS=ON \
    -DCMAKE_CXX_FLAGS="--coverage -O0 -g" -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build_cov -j
ctest --test-dir build_cov --output-on-failure
python -m gcovr --root . --filter "include/wfc/" --filter "src/" \
    --html-details docs/coverage/index.html build_cov
```

Snapshot de couverture (99% lines, 925 / 933) :

| Module                           | Couverture | Manquant                                      |
|----------------------------------|------------|------------------------------------------------|
| `include/wfc/Bitset.hpp`         | 100%       |                                                |
| `include/wfc/Grid.hpp`           | 100%       |                                                |
| `include/wfc/Wave.hpp`           | 100%       |                                                |
| `include/wfc/Tile.hpp`           | 100%       |                                                |
| `include/wfc/TileSet.hpp`        | 100%       |                                                |
| `include/wfc/OverlapRules.hpp`   | 100%       |                                                |
| `include/wfc/internal/SolverCommon.hpp` | 100% |                                                |
| `include/wfc/internal/WFCSolverBase.hpp` | 100% |                                               |
| `include/wfc/WFCSolver.hpp`      | 100%       |                                                |
| `src/TileSet.cpp`                | 100%       |                                                |
| `src/GridIO.cpp`                 | 100%       |                                                |
| `src/solvers/WFCSolverSerial.cpp`| 100%       |                                                |
| `src/solvers/WFCSolverOMP.cpp`   | 100%       |                                                |
| `src/solvers/WFCSolverKokkos.cpp`| 99%        | exception path `MAX_WORDS_PER_CELL > 8`        |
| `src/internal/SolverCommon.cpp`  | 98%        | accolade fermante `build_output`               |
| `src/internal/WFCSolverBase.cpp` | 94%        | branche all-attempts-failed dans `solve_parallel` + chrono lines (gcov noise) |
| `src/OverlapRules.cpp`           | 95%        | accolade fermante `build`                      |
| TOTAL                            | 99%        | 8 lignes non couvertes / 933                   |

Les 8 lignes manquantes restantes :
- 2 accolades fermantes (line counters sur `}` à la fin de fonctions)
- 2 lignes splittées de constructeurs `std::chrono::duration<double>(...)`
  inline (gcov tracke les destructeurs de temporaires sur la même ligne
  source mais ne valide pas toujours leur exécution)
- 2 lignes de la branche "tous les batches ont échoué" dans
  `solve_parallel` (les workloads de test sont assez tolérants pour ne
  jamais épuiser tous les batches en parallel-attempts mode)
- 2 lignes de l'exception `MAX_WORDS_PER_CELL > 8` dans
  `WFCSolverKokkos::propagate` (aucun sample n'a > 512 tuiles)

Toutes les lignes fonctionnelles testables sont couvertes.

## Framework

Pas de framework externe. `tests/test_helpers.hpp` (40 lignes) fournit
deux macros :

```cpp
WFC_CHECK(condition)         // booléenne simple
WFC_CHECK_EQ(a, b)           // affiche les valeurs en cas d'échec
```

Compteurs globaux `g_total` / `g_failures`, rapport via
`wfc_test::report()` à la fin du `main`. Pour ajouter un test, on
ajoute un fichier `tests/test_xxx.cpp`, on l'enregistre dans
`CMakeLists.txt`, et c'est tout.

## Couverture par module

### `Bitset` ([test_bitset](../tests/test_bitset.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Bitset(N) initialisé à zéro                              | OK      |
| `Bitset::full(N)` met exactement N bits, trim correct du tail | OK |
| set/clear/test cohérents                                 | OK      |
| count() = nombre de bits set                             | OK      |
| any() = il existe un bit set                             | OK      |
| first_set() = plus petit index set, sentinelle = nbits  | OK      |
| `for_each_set` visite en ordre croissant, skip mots vides | OK     |
| `and_with` retourne true ssi au moins un bit a été cleared | OK    |
| `and_with` est correct sur multi-mots                    | OK      |
| `or_with` accumule sans perdre de bits                   | OK      |
| `set_only(i)` collapse à un seul bit                     | OK      |
| Égalité `Bitset == Bitset`                               | OK      |
| Construction depuis ConstBitsetView (snapshot)           | OK      |
| Tail correctement trimé pour N non multiple de 64        | OK      |
| Multi-mots (>= 128 bits) : count, popcount, set, clear   | OK      |

### `Grid` ([test_grid](../tests/test_grid.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Constructeur (rows, cols, fill)                          | OK      |
| rows/cols/size cohérents                                 | OK      |
| at(r, c) lecture/écriture                                | OK      |
| at_torus wrap des deux côtés                             | OK      |
| at_torus avec offsets > rows/cols                        | OK      |
| fill_row_major taille mismatch → exception               | OK      |
| fill_row_major valeur < 0 ou > 255 → exception           | OK      |
| Grid 1×1                                                 | OK      |
| data() retourne le buffer attendu                        | OK      |

### `TileSet` ([test_tileset](../tests/test_tileset.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Total des fréquences = rows × cols (toroïdal)            | OK      |
| max_value est correct                                    | OK      |
| Tuiles attendues présentes dans l'exemple du sujet      | OK      |
| Tuile absente (`{0,0,0,0}`) bien absente                 | OK      |
| Pas de doublon dans `tiles_`                             | OK      |
| Multivalue (4+ valeurs) : extraction marche              | OK      |
| N=1 : chaque tile = 1 cellule                            | OK      |
| N=3 : extraction et fréquences                           | OK      |

### `OverlapRules` ([test_overlap](../tests/test_overlap.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| `allowed(t, 0, 0) = {t}` (identité)                      | OK      |
| Symétrie : `t2 ∈ allow(t1, dx, dy) ⇔ t1 ∈ allow(t2, -dx, -dy)` | OK |
| Tous les couples au max offset (N-1, N-1) testés         | OK      |
| N=3 : symétrie tient                                      | OK      |
| Multivalue : règles cohérentes                            | OK      |
| `allowed(t, dx, dy)` non vide pour tile valide (au moins t lui-même) | OK |

### `GridIO` ([test_grid_io](../tests/test_grid_io.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Round-trip txt : write puis read = identique             | OK      |
| Multivalue round-trip                                    | OK      |
| Commentaires `#` ignorés                                 | OK      |
| Ligne vide ignorée                                        | OK      |
| Largeur de ligne incohérente → exception                 | OK      |
| Fichier vide → exception                                 | OK      |
| Fichier inexistant → exception                            | OK      |
| Valeur out-of-range (>255) → exception                   | OK      |
| PPM : magic P6, dimensions, max value                    | OK      |
| PPM : taille de buffer = W·H·3 + header                  | OK      |
| PNG : signature 89 50 4E 47 ...                          | OK      |
| Scale appliqué correctement (W = cols·scale)             | OK      |

### `Wave` ([test_wave](../tests/test_wave.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Initialisation : tous les bits set sur toutes les cellules | OK    |
| num_cells = rows × cols                                  | OK      |
| Modifs `at(r, c)` persistent                             | OK      |
| Cellules indépendantes (modifier une n'affecte pas l'autre) | OK   |
| `at(cell)` et `at(r, c)` retournent même vue              | OK     |
| `torus_idx` wrap dans les deux directions                | OK      |
| `row(cell)` / `col(cell)` cohérents                      | OK      |
| Wave 1×1                                                  | OK      |
| Différents nombres de tiles (1 mot, multi-mots)          | OK      |

### `SolverCommon` ([test_solver_common](../tests/test_solver_common.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| `weighted_entropy` = 0 sur cellule à 1 candidat          | OK      |
| `weighted_entropy` croît avec le nombre de candidats     | OK      |
| `weighted_entropy` finie, ≥ 0                            | OK      |
| `weighted_pick` deterministe avec même rng               | OK      |
| `weighted_pick` retourne un id présent dans `cell`       | OK      |
| `weighted_pick` respecte les fréquences (test statistique) | OK    |
| `cell_jitter(c, s)` ∈ [0, 1e-6)                          | OK      |
| `cell_jitter` deterministe                                | OK      |
| `cell_jitter` produit des valeurs distinctes pour cellules différentes | OK |
| `attempt_seed` produit une suite distincte                | OK      |
| `serial_min_entropy` retourne -1 quand toutes décidées   | OK      |
| `serial_min_entropy` ignore les cellules à 1 candidat     | OK      |
| `build_output` reconstruit correctement la grille         | OK      |

### Solveur série ([test_solver](../tests/test_solver.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Résoudre l'exemple du sujet sans erreur                  | OK      |
| Same seed → same output                                  | OK      |
| Different seed → different output (avec haute proba)     | OK      |
| `output_uses_only_input_tiles` (soundness)               | OK      |
| Plusieurs tailles (16, 32, 48)                           | OK      |
| Multivalue terrain résolu                                 | OK      |
| Multivalue maze résolu                                    | OK      |
| N=3 sur sample compatible                                 | OK      |
| max_attempts > 1 utilisé en cas de contradiction          | OK      |
| `stats.success` cohérent avec retour                      | OK      |
| `stats.collapses` ≤ rows×cols                             | OK      |

### Solveur OMP ([test_solver_omp](../tests/test_solver_omp.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| OMP avec 1, 2, 4, 8 threads = serial bit-à-bit            | OK      |
| Reseed produit sortie différente                          | OK      |
| Multivalue : OMP = serial                                  | OK      |

### Parallel attempts ([test_parallel_attempts](../tests/test_parallel_attempts.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| `parallel_attempts < 1` → `validate()` throw              | OK      |
| K=4 sur sample facile : sortie = sequential même seed     | OK      |
| K=8 sur sample facile : `attempts == 1` (lowest-success)  | OK      |
| K=8 sur terrain N=3 : orchestration ne crashe pas         | OK      |
| OMP backend avec K=4 = serial backend avec K=4 bit-à-bit  | OK      |
| OMP backend K=1 retombe sur le chemin séquentiel legacy   | OK      |

### Symétries D4 ([test_symmetries](../tests/test_symmetries.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| `Tile::rotated_90()` appliqué 4 fois = identité           | OK      |
| `Tile::reflected_horizontal()` appliqué 2 fois = identité | OK      |
| `from_sample(g, N)` (legacy) = `from_sample(g, N, 1)`     | OK      |
| Sample uniforme : S=1, 2, 4, 8 → tile set de taille 1     | OK      |
| Sample asymétrique : taille croissante S=1 ≤ 2 ≤ 4 ≤ 8   | OK      |
| `symmetries ∈ {0, 3, -4}` → `std::invalid_argument`       | OK      |
| Auto-symétrie ne double-compte pas la fréquence           | OK      |

### Backtracking ([test_backtrack](../tests/test_backtrack.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| `use_backtracking=false` (défaut) inchangé bit-à-bit      | OK      |
| Sample facile + backtrack : succès, soundness             | OK      |
| Sample serré (terrain N=2) où retry-1 échoue : succès     | OK      |
| Même seed + backtrack → même sortie (deux solveurs)        | OK      |

### Solveur Kokkos ([test_solver_kokkos](../tests/test_solver_kokkos.cpp))

| Invariant                                                | Vérifié |
|----------------------------------------------------------|---------|
| Kokkos = serial bit-à-bit                                  | OK      |
| Reseed différent                                           | OK      |

## Ce qui n'est PAS couvert (volontairement ou pas)

| Aspect                                | Pourquoi pas testé                                    |
|---------------------------------------|-------------------------------------------------------|
| Performance                           | tests = correctness ; perf = benchmarks               |
| CLI parsing                           | trivial, déléguer à des tests d'intégration          |
| Apps complets (`wfc_serial`, etc.)    | testés indirectement par les tests solveur + benchmark |
| Gros samples (>32×32)                  | trop lent pour ctest, mais indirectement via benchmark |
| Wraparound de Value (uint8_t)         | pas de scénario de test naturel                        |
| Retour `Grid::at_torus(0, 0, 0, 0)` quand grid est 0×0 | UB (pas de cas d'usage légitime) |
| Backtrack sur GPU (Kokkos CUDA)       | snapshot/restore single-thread par construction, non applicable |

## Race detection

Les tests `test_solver_omp` et `test_solver_kokkos` sont conçus pour
détecter les régressions de déterminisme. Pour une vérification plus
poussée des races :

```bash
cmake -B build_tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_OMP=ON -DUSE_TSAN=ON
cmake --build build_tsan -j
ctest --test-dir build_tsan --output-on-failure
```

ThreadSanitizer ralentit les tests d'environ 5-10× mais détecte les
data races qu'un test fonctionnel pourrait laisser passer.

## Ajouter un test

1. Créer `tests/test_xxx.cpp` qui inclut `test_helpers.hpp` et un module
   du projet.
2. Utiliser `WFC_CHECK` et `WFC_CHECK_EQ` ; finir par `return wfc_test::report();`.
3. Enregistrer dans `CMakeLists.txt` :
   ```cmake
   add_executable(test_xxx tests/test_xxx.cpp)
   target_link_libraries(test_xxx PRIVATE wfc_core)
   add_test(NAME test_xxx COMMAND test_xxx)
   ```
4. `cmake --build build && ctest --test-dir build`.

Si le test exerce un solveur parallèle, mettre dans le bloc
`if(USE_OMP)` ou `if(USE_KOKKOS)` correspondant.

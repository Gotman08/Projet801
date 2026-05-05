# Algorithme WFC tel qu'implémenté

Pour le squelette pédagogique de l'algorithme, voir [README.pdf](../README.pdf).
Pour la structure du code, voir [ARCHITECTURE.md](ARCHITECTURE.md).
Ce document détaille ce que fait précisément chaque étape de notre
implémentation.

## Vue d'ensemble

```
Inputs  : sample S (grille rows×cols), problem grid G (rows'×cols'), tile size N
Outputs : G remplie, ou échec si contradiction non résolue

1. Extract       T, freq      ← from_sample(S, N)              [TileSet]
2. Build rules   R(t, dx, dy) ← compatibility tables            [OverlapRules]
3. Init          W(r, c)      ← all tiles possible, every cell  [Wave]
4. Loop:
   a. cell ← argmin_c entropy(W[c])                              [pick_cell]
   b. if cell is None: success → return G
   c. tile ← weighted_random(W[cell], freq)
   d. W[cell] ← {tile}                                           [collapse]
   e. if propagate(cell) returns contradiction:                  [propagate]
        retry with new seed (up to max_attempts) or fail
5. Reconstruct  G[r,c] ← top-left value of W[r,c]                [build_output]
```

## Étape 1 : Extraction des tuiles

[`TileSet::from_sample(grid, N)`](../src/TileSet.cpp)

Pour chaque position `(r0, c0)` de `S` :
1. Lire `N×N` valeurs en commençant à `(r0, c0)`, avec **wrap toroïdal**
   (`grid.at_torus(r0+i, c0+j)`).
2. Créer une `Tile` (vecteur plat de `N²` valeurs).
3. Insérer dans une `unordered_map<Tile, int>` ; si déjà présente,
   incrémenter sa fréquence.

Résultat : `tiles_` (vecteur des tuiles uniques) et `freq_` (vecteur
parallèle des fréquences). Total des fréquences = `rows × cols`.

**Pourquoi toroïdal** : voir [CHOICES.md](CHOICES.md).

## Étape 2 : Pré-calcul des règles d'adjacence

[`OverlapRules::build(tiles)`](../src/OverlapRules.cpp)

Pour chaque paire `(t1, t2)` de tuiles et chaque offset
`(dx, dy) ∈ [-(N-1), N-1]²`, on dit que `t2` est *compatible* avec `t1`
au décalage `(dx, dy)` si leurs valeurs coïncident sur la zone de
recouvrement :

```
        +-----------> dx
        |  ┌─────┐
        |  │ t1  │       t2 placée à (dx, dy) du coin haut-gauche de t1
        |  │  ┌──┴──┐
        |  └──┤  t2 │
        v     └─────┘
        dy

   pour tout (x, y) dans l'intersection :
        t1[y][x] == t2[y - dy][x - dx]
```

Le test est implémenté par `compatible(t1, t2, N, dx, dy)`.

Stockage du résultat : un tableau plat de `Bitset` indexé par
`tile_id × (2N-1)² + offset_index(dx, dy)`. Pour chaque `(t1, dx, dy)`,
le bitset énumère les `t2` compatibles.

**Symétrie** : `t2 ∈ allowed(t1, dx, dy) ⇔ t1 ∈ allowed(t2, -dx, -dy)`.
Vérifié par [test_overlap](../tests/test_overlap.cpp).

**Cas particulier** : pour `(dx, dy) = (0, 0)`, la zone de recouvrement
est le tile entier, donc `t2` est compatible avec `t1` ssi
`t1 == t2`. Le bitset à offset `(0, 0)` est donc l'identité (un bit par
tile, à sa propre position).

**Coût** : `L² × (2N-1)²` paires à tester, chaque test
`O(N²)`. Total `O(L² × N² × (2N-1)²)`. Pour `L = 11`, `N = 2`, on a
~120 paires × 9 offsets × 4 cellules = ~4500 comparaisons, exécutées
en quelques µs. Pour `L = 73`, `N = 3`, ~530k comparaisons, encore
quelques ms. Boucle parallélisée par `omp parallel for` (gain ~2× sur 8 cores).

## Étape 3 : Initialisation de la Wave

Pour chaque cellule `(r, c)` de la grille de sortie : `W(r, c) = bitset_full(L)`
(toutes les tuiles encore possibles).

Implémenté dans le constructeur de [`Wave`](../include/wfc/Wave.hpp) avec
*first-touch parallèle* (chaque thread initialise les cellules qu'il va
ensuite traiter, mappant les pages physiques sur son NUMA node). Sans
ça, sur EPYC 9654 (8 NUMA nodes), 7/8 des accès traverseraient un
NUMA boundary.

## Étape 4 : Boucle principale

### 4a. Sélection de la cellule d'entropie minimale

L'entropie d'une cellule est calculée par
[`weighted_entropy(cell, freq)`](../src/internal/SolverCommon.cpp) :

```
entropy(cell) = log(Σ f_t) - (Σ f_t · log(f_t)) / Σ f_t
                où la somme porte sur les t encore possibles dans cell
```

C'est l'entropie de Shannon des fréquences normalisées des tuiles
encore candidates. Une cellule à 1 candidat a entropie 0 (déjà décidée),
une cellule avec toutes les tuiles candidates a entropie maximale.

Pour départager les ex-aequo (très fréquents en début de résolution
quand toutes les cellules ont la même entropie), on ajoute un bruit
déterministe `cell_jitter(cell, seed)` ∈ [0, 1e-6) calculé par
SplitMix64.

Trois implémentations de la sélection :
- **Serial** : scan linéaire `O(rows × cols × L)` ([`serial_min_entropy`](../src/internal/SolverCommon.cpp))
- **OMP** : chunked en `~total / (4·p)` cellules par task, partials
  agrégés en ordre déterministe ([`parallel_min_entropy`](../src/solvers/WFCSolverOMP.cpp))
- **Kokkos** : `Kokkos::parallel_for` sur les chunks, même structure
  que OMP ([`kokkos_min_entropy`](../src/solvers/WFCSolverKokkos.cpp))

Si toutes les cellules ont entropie ≤ 1 (toutes décidées), retourne
`-1` → succès.

### 4b. Collapse

Une fois la cellule choisie, on tire une tuile selon
[`weighted_pick(cell, freq, rng)`](../src/internal/SolverCommon.cpp) :
- somme cumulative des fréquences des candidats
- `r = uniform(0, total)`
- on parcourt les candidats, on retourne le premier dont la CDF dépasse `r`

`wave[cell].set_only(tile)` réduit le bitset à un seul bit.

### 4c. Propagation

[`propagate`](../src/solvers/WFCSolverSerial.cpp) (BFS niveau-synchrone)

À partir de la cellule modifiée, on visite ses voisins dans un rayon
`(2N-1) × (2N-1)`. Pour chaque voisin :

1. Calculer l'**union** des tuiles compatibles avec les candidats de
   la cellule courante :
   ```
   allowed = ⋃_{t ∈ wave[cell]} R(t, dx, dy)
   ```
2. **Intersection** : `wave[neighbor] ←  wave[neighbor] ∩ allowed`
3. Si l'intersection a changé, ajouter le voisin à la frontière BFS
   du niveau suivant.
4. Si l'intersection devient vide → contradiction, on retourne `false`.

Variantes :
- **Serial** : `std::queue<int>` classique
- **OMP** : frontière par niveau, `parallel for` interne au niveau via
  tasks, écritures atomiques `__atomic_fetch_and` sur les mots `u64`
  des bitsets voisins
- **Kokkos** : même structure, `Kokkos::atomic_fetch_and` à la place

## Étape 5 : Reconstruction de la sortie

[`build_output(wave, tiles)`](../src/internal/SolverCommon.cpp)

Pour chaque cellule `(r, c)` :
- récupérer le seul tile-id encore possible
- la valeur de pixel `G[r, c]` est `tile.at(0, 0)` (la valeur en haut-à-gauche)

Convention nécessaire parce qu'avec le modèle overlapping, plusieurs
tuiles couvrent chaque pixel ; on choisit conventionnellement celui
dont le coin haut-gauche est sur `(r, c)`.

## Échecs et retentatives

Quand `propagate` retourne `false` (contradiction), `run_attempt`
retourne `false`. La boucle `solve()` retente avec un nouveau seed
dérivé par `attempt_seed(base_seed, attempt - 1)`. Jusqu'à
`max_attempts` essais.

Si tous les essais échouent, on retourne une grille vide (toutes
zéros) et `stats.success = false`.

## Parallel attempts

`SolverOptions::parallel_attempts = K > 1` change le retry séquentiel
en orchestration parallèle :

```
For batch_start in 0, K, 2K, ... < max_attempts:
   batch_size = min(K, max_attempts - batch_start)
   #pragma omp parallel for num_threads(batch_size)
   for k in 0..batch_size:
      seed = attempt_seed(base_seed, batch_start + k)
      success[k] = serial_run_attempt(waves[k], ...)
   picked = first k where success[k] is true (lowest index)
   if picked >= 0: return waves[picked]
```

Détails :

- Chaque attempt utilise `serial_run_attempt` (de SolverCommon),
  jamais le `propagate` du backend → pas d'oversubscription quand
  l'utilisateur demande `WFCSolverOMP` avec K > 1.
- *Cooperative bail-out* : si l'attempt `j` réussit, les attempts
  `k > j` qui n'ont pas encore commencé skipent leur travail (ils
  perdraient face à `j` de toute façon). Les attempts `k < j` qui
  tournent encore continuent, l'un d'eux pourrait gagner avec un
  index plus bas.
- Déterminisme préservé : même seed → même sortie qu'un retry
  séquentiel. Le succès d'index minimum d'un batch est exactement ce
  qu'un retry séquentiel aurait choisi (les attempts d'index
  inférieurs auraient échoué avant).

Quand l'utiliser : workloads à fort taux d'échec par attempt (ex.
`terrain_N3` 24×24 ~10% de succès, gain wallclock 2.14× à K=8 sur
i9-10900K). Inutile sur les workloads qui réussissent du premier
coup, K attempts en parallèle = K× le travail pour le même
résultat.

## Symétries D4 (option `--symmetries S`)

Extension classique de WFC : chaque tuile extraite peut générer ses
variantes par rotation et réflexion. `TileSet::from_sample(grid, N, S)`
implémente le groupe diédral D4 (`S ∈ {1, 2, 4, 8}`) :

- S=1 : identité seule (défaut, comportement legacy bit-à-bit)
- S=2 : rotation 180°
- S=4 : 4 rotations (0°, 90°, 180°, 270°)
- S=8 : 4 rotations + 4 réflexions horizontales

L'expansion se fait à l'extraction, le solveur ne voit qu'un `L` plus
grand. Les patterns auto-symétriques (uniformes, damiers) sont
dédupés par hash de contenu pour ne pas double-compter leur fréquence.

Bénéfice : sur des samples avec une orientation préférée (chemins,
escaliers, branchages), `--symmetries 4` permet d'appliquer le motif
uniformément dans toutes les orientations. Coût : `L` × jusqu'à 8 →
bitsets parfois à 2 mots → solveur ~1.5× plus lent.

## Backtracking (option `--backtrack`)

Sur les samples très contraints, restart-on-contradiction épuise
`max_attempts` sans trouver de solution. `--backtrack` remplace cette
stratégie par un parcours arborescent du domaine.

```
stack = []
loop:
   cell = serial_min_entropy()
   if cell < 0: success
   open frame {cell, candidates_sorted_by_freq_desc, empty_delta}
   try_top()  # essaie un candidat, propage, capture delta
   if forward progress: continue
   else:
      while stack non-empty:
         apply_inverse(stack.top.delta)
         if stack.top has more candidates: try_top()
         else: pop stack
      if stack empty: failure
```

Choix de design (détaillés dans [CHOICES.md](CHOICES.md)) :

1. **Delta-encoded snapshot** : chaque frame stocke uniquement les
   cellules effectivement modifiées par sa propagation. ~80× moins
   de mémoire qu'un snapshot complet.
2. **Choix triés par fréquence descendante** : essai du candidat le
   plus probable en premier.
3. **Bypass backend parallèle** : snapshot/restore est single-thread
   par construction.
4. **Composition avec parallel-attempts** : `--parallel-attempts K
   --backtrack` lance K recherches indépendantes (seeds différents,
   arbres d'exploration différents).

Mesure : terrain N=3 32×32, retry-30-attempts échoue en 0.38 s,
`--backtrack` résout en 0.12 s.

## Complexité

| Étape                | Complexité                                | Pour 128×128, L=11    |
|----------------------|--------------------------------------------|-----------------------|
| Extraction tuiles    | O(rows·cols·N²)                           | ~100 µs               |
| Règles d'adjacence   | O(L² · (2N-1)² · N²)                      | ~50 µs                |
| Min-entropie         | O(rows·cols·L) **par collapse**           | ~30 µs                |
| Propagation         | O(rows·cols·(2N-1)²·L) amorti **par collapse** | ~150 µs           |
| Total (8000 collapses) | dominé par min-entropie + propagation   | ~3-4 s en serial      |

Sur grandes grilles, la min-entropie domine (~85% du temps), c'est sur
cette étape que la parallélisation paie le plus. Notre première
intuition était que la propagation dominerait ; le profilage a montré
le contraire et a orienté l'effort vers `parallel_min_entropy` /
`kokkos_min_entropy`.

## Garanties

| Invariant                                                              | Vérifié par                  |
|------------------------------------------------------------------------|------------------------------|
| Sortie utilise uniquement des tuiles présentes dans l'échantillon     | `test_solver` (`output_uses_only_input_tiles`) |
| Symétrie des règles d'overlap (`t2 ∈ allow(t1, d) ⇔ t1 ∈ allow(t2, -d)`) | `test_overlap` |
| Identité au décalage (0, 0) : `allow(t, 0, 0) = {t}`                  | `test_overlap` |
| Total des fréquences = rows × cols (toroïdal)                          | `test_tileset` |
| Déterminisme : same seed → same output                                  | `test_solver`, `test_solver_omp`, `test_solver_kokkos` |
| Bit-identité serial = OMP = Kokkos pour un même seed                   | `test_solver_omp`, `test_solver_kokkos` |
| Reseed produit une sortie différente                                    | `test_solver_omp`, `test_solver_kokkos` |

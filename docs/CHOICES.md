# Choix techniques

Pourquoi le code est écrit comme il est. Une décision par section.

## Modèle WFC : overlapping uniquement

Maxim Gumin distingue deux variantes de WFC :
- **Tiled model** : palette finie de tuiles, contraintes d'adjacence
  données explicitement. Adapté aux jeux avec sprites.
- **Overlapping model** : tuiles extraites d'un échantillon, contraintes
  déduites par recouvrement.

Le sujet décrit le modèle *overlapping*, c'est donc lui qui est
implémenté. Les deux modèles partagent le même squelette de propagation,
mais l'overlapping demande une étape d'extraction et de calcul de règles
plus lourde, donc plus de surface de profilage.

## Bitset packé sur `uint64_t` plutôt que `std::vector<bool>` ou `std::bitset<N>`

Trois raisons :

1. **Taille dynamique requise**, `std::bitset<N>` exige `N` en constexpr,
   incompatible avec un `L` (nombre de tuiles) déterminé à runtime selon
   l'échantillon.
2. **Vectorisation**, `std::vector<bool>` interdit l'accès direct aux
   mots sous-jacents. On veut faire des `__builtin_popcountll` et des
   AND/OR 64 bits à la fois. Notre `Bitset` expose `words()` directement.
3. **Allocation**, `std::vector<bool>` reste un wrapper sur `vector<u8>`,
   pas plus dense. Notre `Bitset` est exactement `⌈L/64⌉` mots de 64 bits.

Pour `L = 11` (l'exemple du sujet), le bitset tient dans un seul mot.
Pour `L = 73` (multivalue terrain N=3), dans deux mots. Toujours en cache L1.

## `Wave` à buffer plat, pas `std::vector<Bitset>`

Naïvement on mettrait un `std::vector<Bitset>` (un Bitset par cellule).
Mauvais :
- Chaque `Bitset` possède sa propre `std::vector<u64>` → autant
  d'allocations que de cellules. Pour 256×256, ce sont 65 536 allocations.
- Indirection mémoire supplémentaire à chaque accès cellule (pointeur
  vers le buffer interne du vector).

Choix : un seul `std::vector<u64>` contigu de taille
`rows × cols × words_per_cell`, et les accesseurs `at(r, c)` retournent une
`BitsetView` non-propriétaire qui pointe dans ce buffer. Une seule
allocation pour toute la wave, layout cache-friendly, NUMA-friendly.

## `Value = uint8_t` plutôt que template

On aurait pu paramétrer la grille par le type de la valeur
(`Grid<int>`, `Grid<uint8_t>`, ...). Choix de simplicité : `uint8_t` partout,
qui couvre 256 valeurs distinctes. Suffisant pour tous les cas pratiques
(le sujet mentionne « plus de deux valeurs », pas 1000). Conséquences :
- pas de templates partout dans le code (compilation plus rapide, erreurs
  plus lisibles)
- l'extension multi-valeurs ne demande aucun changement de type.

## Tile en `struct` POD plutôt que classe avec `getter`s

`Tile` est manipulé par valeur dans des `unordered_map` et des `vector`
chaud. Moins on a d'accesseurs/constructeurs custom, mieux le compilateur
optimise. Le seul comportement non trivial est `operator==` et le hash,
factorisés dans `TileHash`.

## Échantillonnage **toroïdal** plutôt que linéaire

Le README du sujet ne précise pas si l'extraction wrap ou pas. Choix
toroïdal pour deux raisons :
- chaque pixel apparaît exactement dans `N×N` tiles → distribution uniforme
  de la couverture
- le nombre de tiles extraites = `rows × cols` (pas `(rows-N+1) × (cols-N+1)`),
  ce qui évite les biais de bord
- la propagation utilise la même convention toroïdale → cohérence

C'est aussi le choix de l'implémentation référence de Gumin.

## Sélection min-entropie avec **entropie de Shannon pondérée par les fréquences**

Plusieurs métriques possibles pour départager les cellules d'égale
incertitude :
- nombre de candidats (entropie cardinale, simple)
- entropie de Shannon non pondérée
- entropie de Shannon pondérée par les fréquences

Choix : la dernière. Les motifs fréquents dominent les rares, ce qui
suit l'échantillon original. Calculée par `weighted_entropy`
([SolverCommon.cpp](../src/internal/SolverCommon.cpp)).

## Tirage pondéré par les fréquences

Conséquence directe du choix précédent : `weighted_pick` consomme un
réel uniforme et le compare à la CDF cumulative des fréquences. Cas
limite numérique : si la somme des poids vaut 0 (ne devrait pas
arriver mais on couvre), on retourne le dernier candidat vu pour ne
pas planter.

## Bruit d'entropie **déterministe** plutôt qu'aléatoire

Le tie-breaking entre cellules de même entropie est nécessaire pour
garder le déterminisme :

- Avec un `uniform_real_distribution` qui consomme l'état du RNG,
  l'ordre de consommation dépend du parcours → l'ordre de parcours
  est différent en parallèle → les outputs divergent.
- Solution : on calcule un hash SplitMix64 de `(cell_id, seed)`
  et on l'ajoute à l'entropie. Stateless, identique quel que soit
  l'ordre. Implémenté dans [`cell_jitter`](../include/wfc/internal/SolverCommon.hpp).

Effet : `serial`, `omp` (1/2/4/8 threads) et `kokkos` produisent des
sorties bit-identiques pour un même seed, vérifié par les tests
`test_solver_omp` et `test_solver_kokkos`.

## OpenMP : `#pragma omp task` plutôt que `parallel for`

Demande explicite du sujet. C'est aussi le bon choix technique :

- la frontière BFS varie de taille à chaque niveau (de 1 à plusieurs
  centaines de cellules) → tasks dynamiques amortissent mieux qu'un
  `parallel for` à granularité fixe
- les tasks permettent de garder une seule région `parallel` ouverte
  pour toute la propagation. Sans ça, le coût fork/join à chaque niveau
  BFS dominait largement le travail utile (~50µs fork vs ~1µs travail
  sur les niveaux courts). Mesure observée pendant le développement.

## Une seule région `parallel` pour toute la propagation

Implication du point précédent. Le pattern :
```cpp
#pragma omp parallel
{
    while (!finished) {
        #pragma omp single
        for (chunks) #pragma omp task ...
        #pragma omp single
        swap(frontier, next);
    }
}
```
Permet de réutiliser le pool de threads entre niveaux. Sans ça, perf
catastrophique sur les samples avec beaucoup de niveaux courts.

## Atomic AND **relaxed** plutôt que sequentially consistent

Sur x86, `__atomic_fetch_and` avec `__ATOMIC_RELAXED` génère un
`lock and`. La sémantique relaxed suffit ici parce que :
- AND est associatif et commutatif → ordre indifférent
- on lit `wave.at(c).any()` après chaque écriture pour détecter les
  contradictions ; `any()` n'a pas besoin de fence non plus parce qu'on
  ne s'engage à propager qu'une fois le niveau terminé
- la barrière implicite à la fin du niveau `omp single` joue le rôle de
  fence pour la suite

Pas mesuré l'écart avec acq/rel ; la sémantique relaxed est juste
strictement suffisante donc on prend la moins chère.

## Optimisation locale : skip `lock and` quand le mask serait un no-op

```cpp
const u64 cur = __atomic_load_n(&d[i], __ATOMIC_RELAXED);
if ((cur & ~mask) == 0ULL) continue;  // AND would not change anything
const u64 old = __atomic_fetch_and(&d[i], mask, __ATOMIC_RELAXED);
```

L'opération `lock and` coûte ~80-3000 ns selon la distance NUMA. Une
lecture relaxed coûte juste un MOV. Quand le mask est une superset de
la cellule (cas fréquent quand le voisin est déjà bien contraint), on
évite complètement le lock. Mesuré : ~50% de trafic atomique en moins
sur les workloads typiques.

## `OffsetScratch` aligné `alignas(64)`

Chaque thread maintient un scratch (un Bitset par offset) pour
accumuler les unions. Sans alignement, deux threads adjacents partagent
une cache line → false sharing → perf qui s'effondre.

`alignas(64)` sur la struct + assertion `alignof(...) >= 64` garantit
que `std::vector<OffsetScratch>` place chaque élément sur sa propre
cache line.

## Éviter les allocations dans la hot path

Le solveur appelle `propagate` à chaque collapse (~8000 fois sur 128×128).
Toute allocation à l'intérieur est multipliée par autant. Donc :
- les buffers `frontier_a`, `frontier_b`, `in_queue` sont alloués UNE
  FOIS à l'entrée de `propagate_tasks` puis réutilisés
- les `OffsetScratch` sont pré-alloués pour `max_threads`
- le seul allocateur appelé en hot path est `Bitset src_snapshot{wave.at(c)}`
  pour le snapshot anti-race (1 mot pour `L<=64`, négligeable côté CPU
  sur Linux glibc, sur Windows MSYS, la contention sur le heap lock
  global de mingw fait perdre la perf parallèle, c'est documenté dans
  [PERFORMANCE.md](PERFORMANCE.md))

## Retentatives sur contradiction plutôt que backtracking

Quand WFC échoue (cellule à 0 candidats), deux stratégies :
- **backtracking** : restaurer le dernier point de décision et retenter
- **retry from scratch** : recommencer avec un seed différent

On a choisi la deuxième pour deux raisons :
- backtracking propre = sauvegarder/restaurer l'état complet de la wave,
  potentiellement profond, avec impact mémoire imprédictible
- en pratique, sur les samples fournis, 1-2 attempts suffisent ; un
  full restart prend ~quelques ms, négligeable
- pédagogiquement plus simple et lisible

## Paralléliser `OverlapRules::build`

`#pragma omp parallel for schedule(dynamic)` sur la boucle `t1`. Chaque
`t1` écrit dans une plage disjointe de `rules_`, data-parallel sans
contention. Gain ~2-3× sur 8 cores. Le coût total de la construction
reste négligeable devant la résolution, donc cette optim est plus
"propre" qu'utile.

## CMake : trois libs séparées plutôt qu'une lib monolithique avec ifdef

Bénéfices :
- `wfc_serial` (binaire) ne lie pas OpenMP s'il n'est pas demandé
- on peut compiler `wfc_omp_lib` avec des flags spécifiques (`-fopenmp`)
  sans contaminer le reste
- la dépendance `Kokkos::kokkos` reste localisée à `wfc_kokkos_lib`,
  invisible pour qui n'utilise pas Kokkos

Coût : un CMakeLists légèrement plus verbeux. Acceptable.

## Kokkos : refactor GPU-portable

Le backend Kokkos a été ré-écrit pour compiler avec un backend CUDA / HIP
sans dégrader les perfs CPU OpenMP. Trois changements clés :

1. **Données dans des `Kokkos::View<u64*>`** plutôt que `Wave*` /
   `OverlapRules*` raw pointers. View est trivially-copyable, capturable
   par valeur dans `parallel_for`, et utilisable depuis le device.

2. **Snapshot et `allowed` accumulator sur la stack** (`u64
   snap[MAX_WORDS_PER_CELL]`) au lieu d'allouer un `Bitset` à chaque
   itération. Prérequis pour GPU (pas de heap) et accélère le CPU
   (moins de pression sur le malloc).

3. **Branche compile-time `kHostOnly`** :
   - Si `MemSpace == HostSpace` (build Kokkos sans GPU) : `wave_view`
     est un `UnmanagedView` qui wrap directement `wave.raw_words()`.
     Zéro copie, perf identique au pre-refactor (3.89s vs 3.91s sur
     128×128, dans le bruit).
   - Sinon (build CUDA / HIP) : `wave_view` est managed. On `deep_copy`
     host → device au début de chaque `propagate` et device → host à
     la fin. Coût supplémentaire mais nécessaire pour GPU.

4. **Cleanup via `Kokkos::push_finalize_hook`** : les caches
   `static thread_local` (rules flatten + scratch) sont vidés pendant
   `Kokkos::finalize`, avant que les destructeurs des `View` ne
   s'exécutent à process exit.

Optims existantes préservées : snapshot anti-race (sous forme de stack
array), skip atomic AND quand `mask == ~0`, CAS dedup sur in_queue,
réduction min-entropie déterministe. Optim ajoutée par parité avec OMP :
relaxed-load avant atomic_fetch_and (lit le mot, skip le `lock and`
quand l'AND serait un no-op).

Limite : `MAX_WORDS_PER_CELL = 8` borne le nombre de tuiles à 512.
Au-delà, le solver lève une exception. Aucun de nos samples ne s'en
approche (max observé : multivalue_terrain N=3 = 73 tuiles = 2 mots).

## Parallel attempts : orchestration au-dessus du backend

Le backend OMP plafonne à ~5-8× speedup intra-attempt sur les workloads
HPC (les barrières BFS et la contention atomique inter-NUMA dominent
au-delà de 16 threads). Plutôt que de paralléliser plus à l'intérieur
d'un attempt, `SolverOptions::parallel_attempts = K > 1` lance K
attempts WFC indépendants en parallèle (chacun sériel sur son propre
`Wave`). Le succès d'index minimum gagne, donc la sortie reste
bit-identique à un retry séquentiel.

Pourquoi ça paie :

- Chaque attempt sériel n'a pas de barrière BFS partagée, pas de
  contention atomique : 0% d'overhead synchronisation.
- Sur workloads à fort taux d'échec par attempt (terrain N=3 ~10% de
  succès), K attempts en parallèle trouvent un succès en ~1 attempt
  wallclock au lieu de ~10. Mesure : 2.14× wallclock à K=8 sur
  terrain_N3 24×24 (i9-10900K).

Pourquoi ça ne paie pas toujours :

- Sur workloads qui réussissent du premier coup (binary_5x5,
  smooth_N3), K attempts = K× le travail pour le même résultat.
- Sur workloads avec peu de retries (succès en 1-2 attempts), gain
  marginal.

L'orchestration appelle `serial_run_attempt` (de SolverCommon),
*pas* le `propagate` du backend. Sinon : K attempts × (8 threads
intra-attempt) = 8K threads contendant pour 8 cores, oversubscription.
Cette séparation rend parallel_attempts compatible avec n'importe quel
backend (Serial, OMP, Kokkos) sans risque de deadlock OMP imbriqué.

## Work-density gate dans `propagate_tasks` : essayée puis rollbacked

Tentative pour atténuer le plateau smooth_N3 (peak 2.23× à 4t puis
régression) : ajouter un second seuil au-dessus du frontier-size :
`frontier_size × num_tiles × (2N-1)² ≥ 50 000` ops. Idée : sérialiser
les niveaux BFS qui n'ont pas assez de travail pour amortir la
barrière OMP.

Pourquoi rollbacked : le seuil aurait sérialisé les niveaux BFS
moyens (50-500 cellules) sur binary_L11. Le peak Romeo de 5.27× à 8
threads dépend précisément de la parallélisation de ces niveaux.
Sans bench Romeo pour confirmer, le risque de régression sur
binary > le gain hypothétique sur smooth.

Conséquence : le plateau smooth_N3 est documenté comme fondamental
([benchmark.md](benchmark.md) § "Plateau smooth_N3"). Contournement
utilisateur : `--threads 4` (sweet spot intra-attempt) ou
`--parallel-attempts K` quand le workload échoue souvent.

Le seuil work-density a été conservé sur `parallel_min_entropy`, où
il ne peut rien casser : il ne kick que quand `total × num_tiles <
50 000` (typiquement grilles 32×32 où la sélection parallèle ne paie
pas le coût de la `parallel region`).

## Symétries D4 : opt-in, zéro coût si désactivées

Le sujet et l'article de référence mentionnent que le tile set peut
être étendu par rotations / réflexions. Cette extension est livrée mais
strictement opt-in via `TileSet::from_sample(grid, N, symmetries)` et
le flag CLI `--symmetries S`.

S valides :
- `1` (défaut) : pas d'expansion. Comportement strictement identique
  à l'ancien code (vérifié bit-à-bit par `test_symmetries`). Aucun
  if-check supplémentaire dans la hot path d'extraction.
- `2` : ajoute la rotation 180° de chaque pattern.
- `4` : ajoute les 4 rotations (0°/90°/180°/270°).
- `8` : groupe diédral D4 complet (4 rotations + 4 réflexions).

Coût : la génération des variantes se fait une fois à l'extraction
(quelques µs sur les samples typiques). Le solver ne sait pas que les
tuiles viennent de symétries, il voit juste un `L` plus grand. Effet
indirect : `L` peut passer de 11 à 88 → 2 mots de Bitset au lieu de 1
→ solver ~1.5× plus lent. C'est cohérent : plus de variantes = plus
de choix par cellule.

Implémentation : `Tile::rotated_90()` et `Tile::reflected_horizontal()`
sont des helpers purs qui retournent un nouveau `Tile`. La déduplication
par hash assure que les patterns auto-symétriques (uniformes, damiers)
ne voient pas leur fréquence double-comptée. Test
`test_symmetries.cpp` vérifie l'invariant fondamental
`rotated_90⁴ = identity` et `reflected² = identity`.

## Backtracking : opt-in, mécanisme de snapshot/restore

Sur les samples très contraints (terrain N=3 sur petite grille), la
stratégie restart-on-contradiction épuise rapidement `max_attempts`
sans trouver de solution. Le backtracking, opt-in via
`SolverOptions::use_backtracking` ou `--backtrack`, explore
l'arbre de recherche au lieu d'abandonner.

Algorithme :
1. À chaque collapse, snapshot complet de la wave + ouverture d'une
   frame `{cell, candidates_restants, snapshot}` sur une pile.
2. Tentative de propagation. Sur succès → continue forward.
3. Sur contradiction → pop choices du frame courant jusqu'à un succès,
   ou backtrack au frame parent si épuisé.
4. Frame parent : restore sa snapshot, essaie son choix suivant.
5. Pile vide → l'arbre entier a été parcouru sans solution → échec.

Choix de design :

- **Snapshot complet** plutôt que delta-encoding. La wave fait
  ~32 KB pour 64×64 binaire, négligeable même avec 1000 frames de pile.
  Delta-encoding aurait demandé d'instrumenter `serial_propagate` pour
  tracker quels mots changent : invasif, et bénéfice marginal.
- **Choix triés par fréquence descendante** au lieu de tirage pondéré
  aléatoire. Le backtracking est déterministe pour un seed donné :
  la tuile la plus fréquente est essayée en premier, ce qui suit
  l'heuristique « try the most likely candidate ». Les seeds différents
  produisent des sorties différentes via `cell_jitter` (entropy ties).
- **Bypass complet du backend parallèle**. `solve_sequential` détecte
  `use_backtracking == true` et invoque directement
  `serial_run_attempt_backtrack` au lieu de `run_attempt`. Le snapshot
  / restore n'a de sens qu'en single-threaded ; combiner backtrack +
  OMP intra-attempt ouvrirait des races inutilement complexes.

Mesure : terrain N=3 32×32 où retry-30-attempts échoue en 0.3 s,
backtrack résout en 0.076 s avec quelques milliers de propagations.

## Tests : pas de framework externe

Pas de gtest, pas de catch2. `test_helpers.hpp` (40 lignes) suffit pour
WFC_CHECK / WFC_CHECK_EQ avec compteur global. Pourquoi :
- moins de dépendances (le projet doit pouvoir builder sans accès
  réseau ou avec un cache spack restrictif)
- les tests vérifient des invariants, pas du mock
- le format ctest natif suffit pour la CI

Limite : pas de paramétrage, pas de fixtures.

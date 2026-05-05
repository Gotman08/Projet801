---
title: "Wave Function Collapse"
subtitle: "Projet 801, implémentation HPC"
author: "Nicolas Marano"
date: "12 mai 2026"
theme: "Madrid"
colortheme: "default"
fontsize: 11pt
aspectratio: 169
---

# Plan

1. Le problème : reproduire un motif local
2. L'algorithme WFC en six étapes
3. Architecture C++ et structures de données
4. Parallélisation : où, pourquoi, comment
5. Résultats sur Romeo (EPYC 9654, 192 cores)
6. Extensions livrées : symétries, backtracking, GPU, UE5
7. Démo et conclusion

# Le problème

À partir d'un échantillon `S` (grille d'entiers), produire une grille `G` plus
grande qui « ressemble localement » à `S` :

\centerline{tout sous-bloc N×N de G doit apparaître dans S.}

. . .

Applications : génération procédurale (jeux, level design), inpainting,
synthèse de textures, motifs cellulaires.

. . .

\textbf{Difficulté :} problème de satisfaction de contraintes globales,
naïvement exponentiel. WFC est une heuristique gloutonne : entropie
minimale d'abord + propagation BFS.

# L'algorithme en 6 étapes

\begin{enumerate}
\item \textbf{Extraire} les tuiles N×N de S, dédupliquer, compter les fréquences
\item \textbf{Pré-calculer} les compatibilités tuile×offset
\item \textbf{Initialiser} la wave : chaque cellule = ensemble de toutes les tuiles
\item \textbf{Sélectionner} la cellule d'entropie minimale
\item \textbf{Choisir} une tuile (probabilité $\propto$ fréquence) et la poser
\item \textbf{Propager} la contrainte aux voisins, puis (4)
\end{enumerate}

. . .

\textbf{Échec} : si une cellule a un set vide, on retente avec un autre seed
(ou on backtrack, livré en option).

# Architecture

```
include/wfc/   Grid, Tile, Bitset, TileSet, OverlapRules, Wave
               WFCSolver (interface) + solvers/
src/           implémentations
apps/          wfc_serial, wfc_omp, wfc_kokkos, benchmark, wfc_dungeon
tests/         13 suites, 24 385 checks, 99% lines
samples/       binaires + multi-valeurs + skyline / plant / rooms
ue5_plugin/    plugin Unreal 5.7 (opt-in via -DBUILD_DUNGEON=ON)
```

. . .

\textbf{Cœur \texttt{wfc\_core}} agnostique du backend. Les libs OMP / Kokkos
s'ajoutent par options CMake (\texttt{USE\_OMP}, \texttt{USE\_KOKKOS}).

# Structures de données

\textbf{Bitset packé} sur \texttt{uint64\_t}
\begin{itemize}
\item AND/OR vectoriels 64 bits à la fois
\item \texttt{popcount} via \texttt{\_\_builtin\_popcountll}
\item \texttt{first\_set} via \texttt{\_\_builtin\_ctzll}
\end{itemize}

. . .

\textbf{Wave} : un seul \texttt{vector<u64>} contigu pour toutes les cellules
\begin{itemize}
\item Pas d'allocation par cellule, layout cache-friendly
\item First-touch parallèle pour NUMA (EPYC 8 NUMA nodes)
\item Atomic AND au niveau du mot pour la concurrence
\end{itemize}

. . .

\textbf{OverlapRules} : tableau plat indexé par
$\text{tile\_id} \times (2N-1)^2 + \text{offset}$ → bitset de tuiles compatibles

# Où paralléliser ? Profil avant tout

| Étape                   | Temps relatif |
|-------------------------|---------------|
| Extraction des tuiles   | < 0.01 % |
| Règles d'adjacence      | < 0.01 % |
| \textbf{Sélection min-entropie} | \textbf{~85 \%} |
| Propagation             | ~12 \% |
| RNG / divers            | ~3 \% |

. . .

\textbf{Surprise} : la sélection domine, pas la propagation.

\textbf{Leçon} : mesurer avant d'optimiser.

# OpenMP avec `#pragma omp task`

\textbf{Sélection min-entropie}

```cpp
#pragma omp parallel
#pragma omp single
for (int k = 0; k < n_chunks; ++k) {
  #pragma omp task firstprivate(k, start, end)
  partials[k] = local_min(start, end);
}
// réduction en ordre de chunk croissant → déterministe
```

. . .

\textbf{Propagation BFS, une seule région \texttt{parallel}}

```cpp
#pragma omp parallel
while (!finished) {
  #pragma omp single
  for (chunks) #pragma omp task process(...);
  #pragma omp single { swap(frontier, next); ... }
}
```

# Déterminisme à travers les backends

Conserver « même seed → même output » sur série / OMP / Kokkos :

\begin{enumerate}
\item \textbf{Jitter d'entropie déterministe} : hash SplitMix64 sur \texttt{(cell\_id, seed)}, pas de RNG consommé pendant la sélection
\item \textbf{Réduction ordonnée} : min en ordre de chunk fixe, pas \texttt{reduction(min:)}
\item \textbf{Atomics} : AND associatif → résultat invariant à l'ordre
\end{enumerate}

. . .

Vérifié par \texttt{diff} byte-à-byte et par \texttt{test\_solver\_omp} /
\texttt{test\_solver\_kokkos} (run sur \{1, 2, 4, 8\} threads avec même seed).

# Speedup mesuré sur Romeo (sample binaire L=11)

| Taille | 1t | 4t | 8t | 16t | 32t | 192t |
|---|---|---|---|---|---|---|
| 32×32 | 1.00× | 1.16× | 0.91× | 0.33× | 0.18× | 0.02× |
| 64×64 | 1.00× | 2.54× | 2.64× | 0.65× | 0.38× | 0.06× |
| 128×128 | 1.00× | 3.39× | 5.27× | 2.60× | 0.87× | 0.11× |
| \textbf{256×256} | \textbf{1.00×} | \textbf{3.65×} | \textbf{6.73×} | \textbf{8.23×} | \textbf{3.91×} | \textbf{0.19×} |

. . .

\textbf{Peak 8.23× à 16 threads} sur 256×256 (51 \% efficacité). Régression
au-delà : barrières BFS sur niveaux courts + contention atomique inter-NUMA.

\textbf{Plate-forme} : AMD EPYC 9654 192c, gcc 14.2, RHEL 9.

# Modèle d'Amdahl

\centerline{\includegraphics[width=0.7\linewidth]{figures/amdahl/amdahl_binary_L11_256.png}}

\textbf{Fit} : \texttt{f = 0.944}, \texttt{R$^2$ = 0.967}, ceiling théorique
17.8×, peak observé à 46 \% du ceiling. Les 54 \% restants sont les coûts
non-Amdahl (NUMA, contention, fork/join répété).

# Optimisations gardées

\begin{itemize}
\item \textbf{Frontier-threshold} : niveaux BFS courts traités en série, +25 \% à 64 threads sur Romeo
\item \textbf{Skip relaxed-load} avant atomic AND, ~50 \% du trafic atomique en moins
\item \textbf{LTO} : +3.4 \% mesuré (A/B sur 11 runs alternés)
\item \textbf{Wave NUMA-friendly} : first-touch parallèle, layout flat
\item \textbf{OffsetScratch alignas(64)}, anti-faux-partage
\end{itemize}

. . .

\textbf{Optims rejetées en A/B} : log-cache thread\_local (-6.7 \%), vector
FIFO (-3 \%), CSE manuel (bruit). Le compilateur \texttt{-O3
-march=native} en faisait déjà autant.

# Multi-valeurs : extension gratuite

L'extension multi-valeurs n'a demandé \textbf{aucune modification du solveur} :

\begin{itemize}
\item valeurs en \texttt{uint8\_t} (256 niveaux possibles)
\item bitsets indexent des identifiants de tuiles, pas des valeurs
\item le cas binaire est juste un cas particulier
\end{itemize}

. . .

Adaptations limitées au rendu : palette qualitative 16 couleurs, scale
configurable. Trois samples multi-valeurs livrés : terrain (4 valeurs),
maze (3 valeurs avec portes), smooth (4 valeurs en gradients).

# Extension 1 : parallel-attempts

Au lieu de paralléliser \emph{dans} un attempt (saturé à 8-16 threads),
lancer K attempts indépendants en parallèle, succès d'index minimum gagne.

\begin{itemize}
\item \texttt{--parallel-attempts K} en CLI
\item Sortie déterministe (= retry séquentiel pour le même seed)
\item Paie sur workloads à fort taux d'échec (terrain N=3)
\end{itemize}

. . .

\textbf{Mesure} : terrain N=3 24×24, total wallclock 4 seeds :

| Mode | Total |
|---|---|
| sequential, 1 thread | 0.51 s |
| --parallel-attempts 8 --threads 8 | 0.24 s (\textbf{2.14×}) |

# Extension 2 : symétries D4

\texttt{--symmetries 1\textbar 2\textbar 4\textbar 8} : expansion du tile set
par rotations + réflexions au moment de l'extraction.

\begin{itemize}
\item S=1 (défaut) : aucun changement, code path strictement identique au legacy
\item S=4 : 4 rotations
\item S=8 : groupe diédral D4 complet
\item Coût : étape one-shot à l'extraction, hot path inchangé
\end{itemize}

. . .

\textbf{Mesure} : skyline N=3 → 79 tuiles à S=1, 247 à S=4, 331 à S=8.
Auto-symétrie déduplique correctement (vérifié par \texttt{test\_symmetries}).

# Extension 3 : backtracking opt-in

\texttt{--backtrack} remplace restart-on-contradiction par parcours
arborescent.

\begin{itemize}
\item Pile de frames \{cellule, choix restants, delta\}
\item \textbf{Delta-encoded snapshot} : 80× moins de mémoire qu'un snapshot complet
\item Composable avec \texttt{--parallel-attempts} : K recherches indépendantes
\item Default off → hot path inchangé
\end{itemize}

. . .

\textbf{Mesure} : terrain N=3 32×32 où retry-30 échoue en 0.38 s,
\texttt{--backtrack} résout en 0.12 s.

# Extension 4 : backend GPU via Kokkos

Refactor pour compiler avec \texttt{Kokkos\_ENABLE\_CUDA}, sans dégrader
les perfs CPU OpenMP.

\begin{itemize}
\item \texttt{Kokkos::View<u64*>} sur \texttt{HostSpace} ou \texttt{CudaSpace}
\item Branche \texttt{kHostOnly} compile-time : zero-copy si CPU, deep\_copy si GPU
\item Stack arrays \texttt{MAX\_WORDS\_PER\_CELL=8} pour éviter heap sur device
\item \texttt{push\_finalize\_hook} pour clear les caches statiques avant \texttt{Kokkos::finalize}
\end{itemize}

. . .

\textbf{Mesure GH200} : binary 128×128 = 5.4 s, 8× plus lent que CPU OMP 8t.
Cause : H↔D copies par propagate dominent. Le port reste un livrable
fonctionnel ; pour vraiment exploiter GPU, batch processing requis.

# Extension 5 : démo Unreal Engine 5

\texttt{wfc\_dungeon} (opt-in via \texttt{-DBUILD\_DUNGEON=ON}) : CLI qui
émet un JSON consommé par un plugin UE 5.7.

\begin{itemize}
\item JSON v1 (mono-étage) ou v2 (multi-étages avec escaliers)
\item Flood-fill BFS de connectivité avec re-roll si pièces isolées
\item Auto-placement d'escaliers entre étages adjacents
\item Plugin UE 5.7 spawn des \texttt{StaticMeshComponent} par cellule
\end{itemize}

. . .

\textbf{Aucun impact perf} : la cible \texttt{wfc\_dungeon} n'est liée ni au
benchmark, ni aux ctests, ni aux solveurs parallèles.

# Galerie

\begin{columns}
\column{0.33\linewidth}
\textbf{skyline} (4 valeurs, N=3)
\includegraphics[width=\linewidth]{figures/results/gallery/skyline_seed1.png}
\column{0.33\linewidth}
\textbf{plant} (4 valeurs, N=3)
\includegraphics[width=\linewidth]{figures/results/gallery/plant_seed1.png}
\column{0.33\linewidth}
\textbf{rooms} (binaire, N=3)
\includegraphics[width=\linewidth]{figures/results/gallery/rooms_seed1.png}
\end{columns}

\centering Inspirés du papier WFC original (Maxim Gumin), reproduits avec
notre solveur. Reproductibles via \texttt{./scripts/render\_gallery.sh}.

# Problèmes rencontrés

\begin{itemize}
\item Convention d'offset du README inverse de l'intuition $\to$ détecté par test de symétrie
\item Première version OMP \emph{plus lente} que serial (fork/join par niveau BFS) $\to$ une seule région \texttt{parallel}
\item Min-entropie sous-estimée comme coût $\to$ profilage avant optimisation
\item Déterminisme : RNG remplacé par hash sur \texttt{(cell, seed)}
\item Régression à $\geq$ 32 threads sur Romeo $\to$ frontier-threshold optim
\item Kokkos GPU : ré-architecture complète pour zero-copy CPU + deep\_copy GPU
\end{itemize}

# Couverture tests + benchmarks

\begin{itemize}
\item \textbf{13 suites de tests}, 24 385 checks, \textbf{99 \% line coverage} (gcovr)
\item Déterminisme bit-à-bit serial = OMP = Kokkos vérifié pour \{1, 2, 4, 8\} threads
\item \textbf{Romeo} : 220+ runs benchmarkés (jobs SLURM 543692 + 544061 + 544356 + 544361)
\item Scripts SLURM reproductibles : \texttt{romeo\_full\_bench}, \texttt{romeo\_optim\_test}, \texttt{romeo\_gpu\_bench}, \texttt{romeo\_parallel\_attempts}
\item Race detection via TSAN, sanitizers via ASAN
\end{itemize}

# Conclusion

Trois backends opérationnels (serial / OMP / Kokkos), \textbf{déterministes},
+ extensions opt-in :

\begin{itemize}
\item Speedup peak \textbf{8.23× à 16 threads} sur 256×256 (Romeo EPYC 9654)
\item Multi-valeurs sans coût additionnel
\item Symétries D4 + backtracking + parallel-attempts livrés
\item Backend GPU CUDA via Kokkos (refactor GPU-portable)
\item Démo UE5 opt-in
\end{itemize}

. . .

\textbf{Pistes restantes :} NUMA-aware partitioning, batch GPU, priority queue
incrémentale pour la sélection.

# Merci

Questions ?

\centerline{\small Code, samples, benchmarks, rapport et galerie disponibles dans le repo.}
\centerline{\small \texttt{github.com/Gotman08/Projet801}}

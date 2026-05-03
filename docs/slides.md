---
title: "Wave Function Collapse"
subtitle: "Projet 801 — implémentation HPC"
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
5. Résultats : speedup et scalabilité
6. Démo
7. Conclusions

# Le problème

À partir d'un échantillon `S` (grille d'entiers), produire une grille `G` plus
grande qui « ressemble localement » à `S` :

\centerline{tout sous-bloc N×N de G doit apparaître dans S.}

. . .

Applications : génération procédurale (jeux, level design), inpainting,
synthèse de textures, motifs cellulaires.

. . .

**Difficulté :** problème de satisfaction de contraintes globales ; naïvement
exponentiel. WFC est une heuristique gloutonne.

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

\textbf{Échec} : si une cellule a un set vide → on retente avec un autre seed.

# Architecture

```
include/wfc/
  Grid, Tile, Bitset, TileSet, OverlapRules, Wave
  WFCSolver (interface)
  solvers/  Serial / OMP / Kokkos
src/
apps/      wfc_serial, wfc_omp, wfc_kokkos, benchmark
tests/     test_grid, test_tileset, test_overlap, test_solver
samples/   binaires + multi-valeurs
```

. . .

\textbf{Cœur \texttt{wfc\_core}} agnostique du backend.
Les libs OMP / Kokkos s'ajoutent par options CMake.

# Structures de données

**Bitset packé** sur `uint64_t`
\begin{itemize}
\item AND/OR vectoriels 64 bits à la fois
\item \texttt{popcount} via \texttt{\_\_builtin\_popcountll}
\item \texttt{first\_set} via \texttt{\_\_builtin\_ctzll}
\end{itemize}

. . .

**Wave** = `vector<Bitset>` de taille rows × cols
\begin{itemize}
\item Accès toroïdal pour la cohérence avec l'extraction
\item Atomic AND au niveau du mot pour la concurrence
\end{itemize}

. . .

**OverlapRules** : tableau plat indexé par
$\text{tile\_id} \times (2N-1)^2 + \text{offset}$ → bitset de tuiles compatibles

# Où paralléliser ? Profil avant tout

| Étape                   | Temps relatif |
|-------------------------|---------------|
| Extraction des tuiles   | < 0.01 % |
| Règles d'adjacence      | < 0.01 % |
| **Sélection min-entropie** | **~85 %** |
| Propagation             | ~12 % |
| RNG / divers            | ~3 % |

. . .

\textbf{Surprise :} la sélection domine, pas la propagation.

\textbf{Leçon :} mesurer avant d'optimiser.

# OpenMP avec `#pragma omp task`

**Sélection min-entropie**

```cpp
#pragma omp parallel
#pragma omp single
for (int k = 0; k < n_chunks; ++k) {
  #pragma omp task firstprivate(k, start, end)
  partials[k] = local_min(start, end);
}
// réduction en ordre de chunk → déterministe
```

. . .

**Propagation BFS — une seule région `parallel` ouverte**

```cpp
#pragma omp parallel
while (!finished) {
  #pragma omp single
  for (chunks) #pragma omp task process(...);
  // implicit taskwait
  #pragma omp single { swap(frontier, next); ... }
}
```

# Déterminisme à travers les backends

Conserver « même seed → même output » sur série / OMP / Kokkos :

\begin{enumerate}
\item \textbf{Jitter d'entropie déterministe} : hash sur \texttt{(cell\_id, seed)}, pas de RNG consommé
\item \textbf{Réduction ordonnée} : min en ordre de chunk, pas \texttt{reduction(min:)}
\item \textbf{Atomics} : AND associatif → résultat invariant à l'ordre
\end{enumerate}

. . .

Vérifié par `diff` byte-à-byte et par `test_solver` (run x2 avec même seed).

# Speedup mesuré (128×128, sample binaire L=11)

| Threads  | Solve médian (s) | Speedup | Efficacité |
|----------|------------------|---------|------------|
| Série    | 3.30             | 1.00 ×  | 100 % |
| OMP 1    | 3.28             | 1.00 ×  | 100 % |
| OMP 2    | 1.83             | 1.79 ×  | 90 %  |
| OMP 4    | 1.14             | 2.88 ×  | 72 %  |
| OMP 8    | 0.98             | 3.34 ×  | 42 %  |

. . .

Plate-forme : g++ 13.3, AMD/Intel × 20 threads, WSL2 Ubuntu 24.04.

# Speedup graphique

\centerline{\includegraphics[width=0.8\linewidth]{figures/speedup.png}}

# Démo : sorties générées

\begin{columns}
\column{0.33\linewidth}
Binaire 64×64
\includegraphics[width=\linewidth]{figures/sample_binary.png}
\column{0.33\linewidth}
Terrain 64×64
\includegraphics[width=\linewidth]{figures/sample_terrain.png}
\column{0.33\linewidth}
Labyrinthe 48×48
\includegraphics[width=\linewidth]{figures/sample_maze.png}
\end{columns}

# Multi-valeurs : extension gratuite

L'extension multi-valeurs n'a demandé **aucune modification du solveur** :

\begin{itemize}
\item valeurs en \texttt{uint8\_t} (256 niveaux possibles) ;
\item bitsets indexent des identifiants de tuiles, pas des valeurs ;
\item le cas binaire est juste un cas particulier.
\end{itemize}

. . .

Adaptations limitées au rendu : palette qualitative 16 couleurs, scale
configurable.

# Problèmes rencontrés

\begin{itemize}
\item Convention d'offset du README inverse de l'intuition $\to$ détecté par
  test de symétrie
\item Première version OMP \emph{plus lente} que serial (fork/join par niveau BFS)
\item Min-entropie sous-estimée comme coût $\to$ profilage avant optimisation
\item Déterminisme : RNG remplacé par hash sur \texttt{(cell, seed)}
\item Échecs de convergence sur N grand : retry avec seed dérivé
\end{itemize}

# Conclusion

\begin{itemize}
\item Trois backends opérationnels (serial / OMP / Kokkos), \textbf{déterministes}
\item Speedup \textbf{3.3× à 8 threads} sur 128×128
\item Multi-valeurs sans coût additionnel grâce au design en \texttt{uint8\_t}
\item ~1.4k lignes de C++ + tests + benchmarks reproductibles
\end{itemize}

. . .

\textbf{Pistes :} backtracking, heuristique avec PQ, symétries de tuiles,
backend GPU via Kokkos.

# Merci

Questions ?

\centerline{\small Code, samples, benchmarks et rapport disponibles dans le repo.}

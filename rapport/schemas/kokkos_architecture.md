# Schéma : Architecture Kokkos avec branche kHostOnly

## Référencé dans
- `chapters/parallelisation_kokkos_gpu.tex`, figure `fig:kokkos_architecture`

## Cible
- Fichier : `results/figures/schemas/kokkos_architecture.png`
- Format : 1600×900, paysage
- Style : diagramme d'architecture / pipeline

## Prompt ChatGPT

```
A clean technical pipeline diagram showing two parallel paths from a
single source code, with a compile-time branch.

TOP: single box "wave: std::vector<u64> (host)".

BRANCH POINT: diamond shape labeled "if constexpr (kHostOnly)" with
two outgoing arrows.

LEFT PATH (CPU build, kHostOnly = true):
- Box: "Kokkos::View<u64*, HostSpace>"
- Arrow with label "UnmanagedView wraps wave.raw_words() (zero copy)"
- Box: "Kokkos::parallel_for on HostSpace (OpenMP backend)"
- Box: "Result in-place in wave"

RIGHT PATH (GPU build, kHostOnly = false):
- Box: "Kokkos::View<u64*, CudaSpace> (managed)"
- Arrow with label "deep_copy host -> device"
- Box: "Kokkos::parallel_for on CudaSpace (CUDA backend)"
- Arrow with label "deep_copy device -> host"
- Box: "Result back in wave"

CONVERGE at bottom: single box "Same result, different perf
characteristics".

Use blue for CPU path, green for GPU path, grey for shared parts.
Arrows clear and labeled. White background. Clean technical style.
```

## Notes
- Important : montrer visuellement que les deux chemins coexistent dans
  la MEME base de code, sélectionnés à la compilation.
- La phrase clé : "zero copy on CPU, deep_copy on GPU".

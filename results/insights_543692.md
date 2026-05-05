# Benchmark insights — 224 rows from results/full_543692.csv

## Optimal thread count per problem

For each (label, size), thread count where OMP achieves its best time.

| Label | Size | Best threads | Speedup | Efficiency |
|---|---|---|---|---|
| binary_L11 | 32x32 | 4 | 1.16× | 29.0% |
| binary_L11 | 64x64 | 8 | 2.64× | 33.0% |
| binary_L11 | 128x128 | 8 | 5.27× | 65.9% |

## Strong-scaling efficiency knee

Thread count at which parallel efficiency drops below 50% (i.e. doubling threads no longer doubles speed).

| Label | Size | Knee at | Eff at peak |
|---|---|---|---|
| binary_L11 | 32x32 | 4t | 84.2% (at 1t) |
| binary_L11 | 64x64 | 8t | 95.7% (at 1t) |
| binary_L11 | 128x128 | 16t | 99.7% (at 1t) |

## Parallel slowdown (omp t > 1 slower than serial)

Configurations where adding threads makes the code slower than serial. Diagnostic: indicates fork/join + sync cost > parallelizable work.

| Label | Size | Threads | Serial (s) | OMP (s) | Slowdown |
|---|---|---|---|---|---|
| binary_L11 | 32x32 | 8 | 0.02 | 0.02 | 1.10× slower |
| binary_L11 | 32x32 | 16 | 0.02 | 0.05 | 2.99× slower |
| binary_L11 | 64x64 | 16 | 0.25 | 0.38 | 1.54× slower |
| binary_L11 | 32x32 | 32 | 0.02 | 0.08 | 5.41× slower |
| binary_L11 | 64x64 | 32 | 0.25 | 0.65 | 2.62× slower |
| binary_L11 | 128x128 | 32 | 3.97 | 4.53 | 1.14× slower |
| binary_L11 | 32x32 | 64 | 0.02 | 0.17 | 11.30× slower |
| binary_L11 | 64x64 | 64 | 0.25 | 1.51 | 6.08× slower |
| binary_L11 | 128x128 | 64 | 3.97 | 13.16 | 3.32× slower |
| binary_L11 | 32x32 | 96 | 0.02 | 0.24 | 16.01× slower |
| binary_L11 | 64x64 | 96 | 0.25 | 1.57 | 6.31× slower |
| binary_L11 | 128x128 | 96 | 3.97 | 16.50 | 4.16× slower |
| binary_L11 | 32x32 | 192 | 0.02 | 0.64 | 42.62× slower |
| binary_L11 | 64x64 | 192 | 0.25 | 3.93 | 15.83× slower |
| binary_L11 | 128x128 | 192 | 3.97 | 35.30 | 8.90× slower |

## Workload sensitivity

Does L (number of unique tiles) affect parallel efficiency?

| Size | binary_L11 |
|---|---|
| 32x32 | 1.16× |
| 64x64 | 2.64× |
| 128x128 | 5.27× |
| 256x256 | — |

## Kokkos thread sensitivity

Kokkos solve times across 'thread' counts. The Kokkos backend
doesn't take a threads argument from `wfc_benchmark` — it uses
the default Kokkos host execution space concurrency. So all
'thread' columns should give the same result; if they do, this
documents that the kokkos sweep is effectively a single-config
run with statistical replicates rather than a scaling study.

### `binary_L11`

| Size | 1t | 2t | 4t | 8t | 16t | 32t | 64t | 96t | 192t |
|---|---|---|---|---|---|---|---|---|---|
| 32x32 | 0.22 | 0.22 | 0.17 | 0.22 | 0.22 | 0.22 | 0.22 | 0.22 | 0.21 |
| 64x64 | 0.80 | 0.82 | 0.84 | 0.79 | 0.83 | 0.83 | 0.84 | 0.80 | 0.82 |
| 128x128 | 3.46 | 3.42 | 3.42 | 3.42 | 3.41 | 3.42 | 3.41 | 3.42 | 3.42 |
| 256x256 | 13.84 | 13.63 | 13.63 | 13.74 | 13.63 | 13.61 | 13.75 | 13.72 | 13.72 |


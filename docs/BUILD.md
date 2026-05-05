# Build

Plateformes testées et leurs spécificités. Si un build échoue, voir le
tableau ci-dessous puis la section correspondante.

## Plateformes testées

| Plateforme              | Compilateur     | OpenMP | Kokkos | Tests | Notes                              |
|-------------------------|-----------------|--------|--------|-------|------------------------------------|
| Ubuntu 24.04 / WSL2     | g++ 13.3        | 4.5    | OK     | 8/8   | machine de développement principale |
| Windows 11 (MinGW UCRT) | g++ 16.1        | 5.2    | OK     | 8/8   | via MSYS2, voir section Windows    |
| Romeo (RHEL 9)          | g++ 14.2        | 4.5    | OK     | 12/12 | spack+sbatch, voir section Romeo   |

Pré-requis communs : C++17, CMake ≥ 3.16, ninja recommandé.

## Linux (Ubuntu / WSL2)

```bash
sudo apt install build-essential cmake ninja-build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Pour activer Kokkos :

```bash
./scripts/build_kokkos.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON -DUSE_KOKKOS=ON \
      -DKokkos_ROOT=$PWD/external/kokkos/install
cmake --build build -j
```

## Windows (natif, sans WSL)

Le code utilise des intrinsics GCC (`__atomic_fetch_and`, ...) — MSVC
**ne fonctionnera pas**. Il faut MinGW-w64 ou Clang.

### MSYS2 + MinGW-w64 UCRT (recommandé)

```powershell
# Installer MSYS2
winget install --id=MSYS2.MSYS2 --exact

# Dans un shell MSYS2 ou via bash.exe
C:\msys64\usr\bin\bash.exe -lc "pacman -Sy --noconfirm \
    && pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-toolchain \
       mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja"
```

Puis dans PowerShell, ajouter le toolchain au PATH puis builder :

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Pour Kokkos sous Windows :

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
git clone --depth 1 --branch 4.4.01 https://github.com/kokkos/kokkos.git external\kokkos\src
cmake -S external\kokkos\src -B external\kokkos\build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release -DKokkos_ENABLE_OPENMP=ON -DKokkos_ENABLE_SERIAL=ON `
    -DCMAKE_INSTALL_PREFIX="$PWD\external\kokkos\install" -DCMAKE_CXX_COMPILER=g++
cmake --build external\kokkos\build -j
cmake --install external\kokkos\build

# Puis configurer le projet
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON -DUSE_KOKKOS=ON `
    -DKokkos_ROOT="$PWD\external\kokkos\install"
cmake --build build -j
```

**Note Windows perf** : voir [PERFORMANCE.md](PERFORMANCE.md). La
parallélisation OpenMP ne scale pas aussi bien sous MinGW que sous
Linux à cause du heap lock global sur Windows et du runtime
winpthreads.

## Romeo (HPC, AMD EPYC 9654)

Romeo n'a pas cmake/ninja accessible directement, il faut passer par spack :

```bash
ssh romeo
cd ~/votre_repo_clone

# Charger l'environnement et la toolchain
romeo_load_x64cpu_env
spack load /56vu72q   # cmake@3.31.8 %aocc — hash exact, voir spack find --long cmake
spack load /tpamt4u   # ninja@1.12.1 %aocc
spack load gcc@14.2.0

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Le script [`scripts/build_romeo.sh`](../scripts/build_romeo.sh) automatise
ces étapes pour le build serial+OMP.

### Build avec Kokkos sur Romeo

Pas dans spack par défaut, à compiler. Le script
[`scripts/build_kokkos_romeo.sh`](../scripts/build_kokkos_romeo.sh) clone
Kokkos 4.4.01 dans `external/`, le build, et reconfigure le projet avec
`USE_KOKKOS=ON USE_LTO=ON`.

```bash
bash scripts/build_kokkos_romeo.sh
```

### Kokkos avec backend GPU (CUDA, sur Romeo armgpu)

Le code `WFCSolverKokkos` est GPU-portable depuis le refactor décrit
dans [CHOICES.md](CHOICES.md). Pour builder avec backend CUDA :

```bash
ssh romeo
cd ~/wfc801
salloc --partition=instant --constraint=armgpu --gpus-per-node=1 --time=01:00:00 --account=r250127
romeo_load_armgpu_env

# Kokkos CUDA build (sources dans external/)
cmake -S external/kokkos/src -B external/kokkos/build_cuda -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DKokkos_ENABLE_CUDA=ON \
    -DKokkos_ENABLE_SERIAL=ON \
    -DKokkos_ARCH_HOPPER90=ON \
    -DCMAKE_INSTALL_PREFIX="$PWD/external/kokkos/install_cuda"
cmake --build external/kokkos/build_cuda -j
cmake --install external/kokkos/build_cuda

# Project build
cmake -B build_cuda -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DUSE_OMP=ON -DUSE_KOKKOS=ON -DUSE_LTO=ON \
    -DKokkos_ROOT="$PWD/external/kokkos/install_cuda"
cmake --build build_cuda -j
```

Avec ce build :
- `wfc_serial` et `wfc_omp` : tournent sur CPU comme d'habitude
- `wfc_kokkos` : la `parallel_for` de propagation tourne sur GPU
  (host → device → host syncs autour de chaque appel à `propagate`)
- min-entropie reste sur CPU (le scan utilise `Kokkos::DefaultHostExecutionSpace`
  parce que la wave est lue par valeur via `Wave::at` dans la lambda)

Cette config n'a pas été testée par manque de temps sur le partition
armgpu, mais le code compile en théorie. Voir
[WFCSolverKokkos.cpp](../src/solvers/WFCSolverKokkos.cpp) pour le détail
des branches `kHostOnly` qui éliminent les deep_copies sur builds
CPU-only et les activent sur builds GPU.

### Soumettre un job benchmark

```bash
sbatch scripts/romeo_smoke.slurm        # smoke test ~15 min
sbatch scripts/romeo_full_bench.slurm   # full sweep ~1h sur 192 cores
```

Les scripts SLURM utilisent l'account `r250127`. Pour un autre compte,
modifier la ligne `#SBATCH --account=...`.

Suivre le job : `squeue --me`.
Récupérer les résultats : `scp romeo:~/wfc801/results/*.csv ./results/`.

## Options CMake

| Option              | Défaut    | Effet                                         |
|---------------------|-----------|-----------------------------------------------|
| `CMAKE_BUILD_TYPE`  | `Release` | `-O3 -march=native` pour Release ; `-O0 -g3` Debug |
| `USE_OMP`           | `ON`      | Build de `wfc_omp_lib`, `wfc_omp`, `test_solver_omp` |
| `USE_KOKKOS`        | `OFF`     | Build de `wfc_kokkos_lib`, `wfc_kokkos`, `test_solver_kokkos` |
| `BUILD_TESTING`     | `ON`      | Build des tests unitaires                     |
| `USE_LTO`           | `OFF`     | Link-Time Optimization (+3.4% sur binary 128×128, double le temps de link) |
| `USE_TSAN`          | `OFF`     | ThreadSanitizer (race detection). Incompatible avec ASAN, LTO |
| `USE_ASAN`          | `OFF`     | AddressSanitizer + UndefinedBehaviorSanitizer (incompatible avec LTO) |

### `USE_LTO` en détail

LTO (Link-Time Optimization) permet au linker d'inliner et d'optimiser à
travers les unités de compilation. Mesure A/B sur la machine de
développement (i9-10900K, MinGW UCRT g++ 16.1, sample binaire L=11,
output 128×128, seed 42, attempts 3, médiane sur 11 runs alternés) :

| Build                | Median   | Speedup vs baseline |
|----------------------|----------|---------------------|
| Release sans LTO     | 4.280 s  | —                   |
| Release + USE_LTO=ON | 4.134 s  | **+3.4%**           |

LTO testé aussi avec PGO en plus : +0.4% de gain supplémentaire, pas
suffisant pour justifier la complexité d'un build en 2 passes. PGO
rejeté, LTO gardé.

LTO est OFF par défaut pour ne pas ralentir le edit-build-test loop
(link passe de ~1s à ~3s avec LTO sur ce projet). À activer pour les
builds de release ou de benchmark.

## Builds spéciaux

### Race detection

```bash
cmake -B build_tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DUSE_OMP=ON -DUSE_TSAN=ON
cmake --build build_tsan -j
ctest --test-dir build_tsan --output-on-failure
```

### Strict warnings

```bash
cmake -B build_strict -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_OMP=ON \
    -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Werror"
cmake --build build_strict -j
```

Le projet build sans warning sous ces flags.

## Si ça plante

| Symptôme                                | Cause probable                                    | Fix                                                    |
|------------------------------------------|---------------------------------------------------|--------------------------------------------------------|
| `error: '__atomic_fetch_and' was not declared` | Compilateur MSVC, pas GCC/Clang             | utiliser MinGW ou Clang                                |
| `Could NOT find OpenMP`                  | `-fopenmp` non disponible                         | installer le runtime OpenMP du compilateur (`libomp-dev`) |
| `Could NOT find Kokkos`                  | `Kokkos_ROOT` mal défini                          | passer le bon chemin avec `-DKokkos_ROOT=...`          |
| `cmake matches multiple packages` sur Romeo | spack a plusieurs cmake installés               | utiliser le hash : `spack load /56vu72q`               |
| `error: stoi argument out of range` à l'exécution | argument numérique trop grand                | vérifier la ligne de commande, voir `--help`           |
| Tests `test_solver_omp` échouent         | bug de déterminisme ou race                       | relancer en `USE_TSAN=ON`, signaler                    |

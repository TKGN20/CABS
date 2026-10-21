# Experiment protocol and server execution

## Recorded local measurements

The checked-in full runs use original public CSR data, ell=24, delta_h=0.2,
k=50, k_max=100, seed=20261007, four disjoint calibration queries, all 6,976
remaining evaluation queries, ten Lloyd iterations, four construction/GT
threads, one search thread, five warm-up queries and three timing repeats.
Each run constructs a fresh automatically calibrated index. Full query IDs,
hash counts, K, B and input/source/binary hashes accompany the measurements.

## Paper server

The manuscript environment is a Dell PowerEdge R740 with two Intel Xeon Silver
4215R processors and 125 GiB RAM. To run on that machine, build on the server
using the same portable Release configuration and original input checksums.
Record its actual OS, compiler, topology and memory rather than copying the WSL
environment into a result manifest.

```bash
lscpu
lscpu -e=CPU,CORE,SOCKET,NODE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCABS_SANITIZE=OFF
cmake --build build -j4
ctest --test-dir build --output-on-failure
CABS_CPUS=0,2,4,6 bash scripts/reproduce_server.sh splade-0.1m --offline --data-directory /data/SPLADE
CABS_CPUS=0,2,4,6 bash scripts/reproduce_server.sh splade-1m --offline --data-directory /data/SPLADE
```

Confirm that the example CPU list selects four physical cores on the intended
NUMA node; adapt it to the machine's topology. The wrapper binds only its own
process tree, retains four build/GT threads, and leaves search sequential. It
does not change system settings or replace automatic K/m/B selection.

The manifest records the runtime environment and GNU time records peak RSS.
Keep raw per-query records and report the server's actual Recall, QPS, IT and IS.
Local and server times must not be converted using assumed hardware speedups.

## Statistics and measurement boundaries

Recall is computed from exact top-50 IDs for the selected corpus. Repeated timing
uses one fixed index and cannot be interpreted as variability over independent
builds. IT includes calibration targets and all necessary calibration indexes;
evaluation GT is separate. IS includes retained search payload and required
original CSR, while process peak RSS is reported separately.

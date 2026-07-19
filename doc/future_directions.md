# Future Directions: Other CPU Stress Approaches

Each direction below targets a distinct CPU subsystem, making them valuable both for stress-testing and for learning what actually loads a CPU.

## A. Memory Bandwidth & Cache Pressure
*   **What**: Stream large arrays through the CPU (sequential reads/writes) sized to exceed L1 → L2 → L3 → RAM, or use random-access pointer-chasing to force cache misses.
*   **Why it loads the CPU**: The core stalls waiting for data from slower memory levels. Shows that CPUs are often *memory-bound*, not compute-bound.
*   **Metric to observe in htop/perf**: high `us` time but low IPC (instructions per cycle); visible as full green bars with lower-than-expected temperature.
*   **Implementation idea**: `stress_memory_bandwidth` — allocate a buffer larger than L3, iterate repeatedly with `volatile` reads/writes.

## B. Floating-Point & SIMD Units
*   **What**: Replace integer arithmetic with `double` or `float` operations, or use AVX/SSE intrinsics to issue 256-/512-bit vector operations.
*   **Why it loads the CPU**: FPUs and vector execution units are separate from integer ALUs. The current `math` mode only exercises the integer pipeline.
*   **Metric to observe**: Higher power draw and temperature than integer math at the same loop rate; visible with `perf stat` FP event counters.
*   **Implementation idea**: `stress_fpu` — tight loop of `sin()`, `sqrt()`, or manual AVX intrinsic chains.

## C. Branch Predictor Stress
*   **What**: Loops with data-dependent, unpredictable conditional branches (e.g., branch direction determined by random data).
*   **Why it loads the CPU**: Mispredicted branches cause pipeline flushes (10–20 cycle penalty each), wasting execution slots without doing useful work.
*   **Metric to observe**: `perf stat -e branch-misses` shows the rate of mispredictions; CPU temperature stays relatively low despite high `us` time.
*   **Implementation idea**: `stress_branches` — iterate over a randomly shuffled boolean array and branch on each element.

## D. Context Switch / Scheduler Stress
*   **What**: Spawn many more threads than CPU cores and have them yield or block frequently, maximizing OS scheduler invocations.
*   **Why it loads the CPU**: Each context switch saves/restores register state and flushes parts of the pipeline and TLB; heavy scheduling appears as red (`sy`) time even without I/O.
*   **Metric to observe**: `vmstat 1` shows high `cs` (context switches/sec); red bars in htop without file I/O.
*   **Implementation idea**: `stress_ctxswitch` — N×cores threads each calling `sched_yield()` in a tight loop.

## E. Atomic / Cache Coherency Stress
*   **What**: Multiple threads repeatedly incrementing a shared `atomic` counter or contending on the same `pthread_mutex`.
*   **Why it loads the CPU**: Forces the cache coherency protocol (MESI) to broadcast invalidations across cores, saturating the inter-core interconnect.
*   **Metric to observe**: Scales poorly with core count (anti-scales); `perf stat -e cache-misses` spikes despite small working set.
*   **Implementation idea**: `stress_atomic` — all threads hammer a single `_Atomic unsigned long` with `__atomic_fetch_add`.

## F. I/O Syscall Variety
*   **What**: Extend beyond `/dev/urandom` to other high-frequency syscall sources: `clock_gettime()`, `pipe` read/write pairs, UDP loopback sockets, or `timerfd`.
*   **Why it's interesting**: Compares the kernel overhead of different syscall paths; `clock_gettime` via vDSO barely enters the kernel, while socket I/O is expensive.
*   **Metric to observe**: `strace -c` shows syscall counts and time; ratio of red to green in htop varies by syscall type.

## G. Mixed / Realistic Workloads
*   **What**: Algorithms that stress multiple units simultaneously — e.g., matrix multiplication (FPU + memory bandwidth), LZ4/zlib compression (integer ALU + memory), SHA hashing (integer ALU + cache).
*   **Why it's interesting**: Real programs are rarely purely compute-bound or memory-bound; mixed workloads reveal bottlenecks that synthetic tests miss.
*   **Implementation idea**: `stress_matrix` — naive O(n³) matrix multiply with large enough matrices to spill out of cache.

## H. Low-Priority (Nice) Stress — Blue Bars
*   **What**: Run any stress method under a high `nice` value (1–19), which tells the OS scheduler to deprioritize the process in favor of normal-priority work.
*   **Why it produces blue**: The kernel accounts niced CPU time separately (`ni` field in `/proc/stat`). htop colors this blue instead of green. The computation itself is identical — only the scheduling priority differs.
*   **How to use**:
    ```bash
    # Maximum nice value — lowest priority
    nice -n 19 ./cpu_stress auto 60 math

    # Or renice a running process
    renice 19 -p <pid>
    ```
*   **Key insight**: Blue bars appear most clearly when the niced process is the only workload. If a normal-priority process competes, it preempts the niced one and the bar shows green for the other process instead. The CPU still runs at 100% utilization — `nice` only affects *who gets the time*, not how much total work the CPU does.

## Summary Table

| Mode | Primary Bottleneck | htop Color | Key `perf` Event |
| :--- | :--- | :--- | :--- |
| `math` (current) | Integer ALU | Green (`us`) | `instructions` |
| `urandom` (current) | Kernel syscall path | Green + Red (`sy`) | `syscalls` |
| Any mode + `nice 19` | Scheduling priority | Blue (`ni`) | — |
| Memory bandwidth | Memory bus / cache | Green (`us`) | `cache-misses` |
| FPU / SIMD | FP execution units | Green (`us`) | `fp_arith` |
| Branch stress | Branch predictor | Green (`us`) | `branch-misses` |
| Context switching | OS scheduler | Red (`sy`) | `cs` (vmstat) |
| Atomic contention | Cache coherency bus | Green (`us`) | `cache-misses` |
| Mixed workload | Multiple | Green (`us`) | varies |

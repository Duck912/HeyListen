# Preparing the shaders: where the time goes (PREP1, 2026-11-18)

A player's builder showed "Prepare the shaders" at 112:39 and 97%, under "Usually about 45 minutes". Nothing of the game is in
this file: counts and times only.

## Where the time goes (RTX 3050, i5-10400F 6 cores / 12 threads, native mesh path, a cold slot, no pipeline notes)

`--prepare-shaders` (vk_draw.cpp `prepare_run`), 550 s wall, 6 workers (half the CPUs), all of them busy the whole time:

| work | thread time | share |
|---|---:|---:|
| the driver's compile of the compute programs (7,781 pipelines, plain and texture variants) | 2,594 s | 79% |
| translation (31,985 programs; SPIR-V emit, packing, cache writes) | 678 s | 21% |

The compute programs go first (6 workers, 460 s of the 550); the graphics programs are the last 90 s. With the pipeline notes
the precompile adds ~300 s (9,995 graphics pipelines on 12 threads) and its compute pass is milliseconds (the pipeline cache has
them already). Per-thread CPU: the process used 6.0 cores of 12 for the whole run: **half the PC was idle**.

Findings:

* The driver's compiler is the CPU's time, one thread per compile, and the workers were `cpus / 2`.
* The bar went by programs: 7% for 8 of 9 minutes, then 90% in the last minute. The builders' time left extrapolates from it.
* The builders' note said "Usually about 45 minutes" for the shaders step whatever the PC.
* Nothing else is big: the program cache's file lock (wait and write time) and zstd packing are in the new `prepare profile` log
  lines, and are small against the compiles.

## What changed (each has a switch)

| change | switch | effect |
|---|---|---|
| workers: all CPUs but an eighth, not half | `RX_VK_PREPARE_WORKERS=<n>` or `old` | see the measurements in the PR |
| driver compiles wait for memory (3 GB of commit left) when another worker is in the driver | `RX_VK_PREPARE_COMMIT_MB=<mb>` (0: off) | more workers cannot run a small PC out of commit |
| with a list of the compute programs play dispatched (`hookshot_computes.bin`), only those are built before the game | `RX_VK_WARM_COMPUTES=all` (as before) | the rest are built at their first dispatch, as on every later start |
| texture variants of compute programs left to first dispatch | `RX_VK_WARM_VARIANTS=base` | opt in |
| progress by cost, not by programs | none | bar and time left proportional to time |
| builders: time left from the step's own progress | none | no more "usually 45 minutes" for this step |
| `prepare profile` log lines | none | where a preparation's time went |

No translated output changes, so neither `HOOKSHOT_SHADER_STAMP` nor any cache or stamp moves: nobody re-prepares because of this.

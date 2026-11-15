# Shader caches and what a translator change throws away (stream K1, 2026-11-15)

## How it works today (main, before RX_VK_SHADER_KEY)

**The stamp.** `engine/nx2/build.py` hashes, with line endings normalised, every file under `gpu/vk/eden/`, `gpu/vk/sirit/` and
`gpu/vk/compat/` plus `vk_shader.cpp`, `vk_bank.cpp`, `vk_state.h` and `vk_api.h`, and compiles the first 64 bits into
`vk_shader.cpp` as `HOOKSHOT_SHADER_STAMP`. At run time `disk_stamp()` (vk_shader.cpp) hashes that value together with the
depth mode, the GPU's mesh-shader support, the C++ library's ABI and `sizeof` of the cached structs, the translation's per-GPU
settings (invariant positions, NaN guard, matrix layout) and every environment variable that starts with one of the
translation switches (`RX_VK_OOT*`, `RX_VK_NAN*`, `RX_VK_MESH*`, `EDEN_*` ...). `program_cache_stamp()` is that hash.
A second, coarser stamp, `HOOKSHOT_STATE_STAMP` (vk_state.h, vk_api.h, vk_draw.h, rx_nvn.h), says what the pipeline notes are laid
out by.

**The files** (all beside the exe):

| File | Holds | Keyed / valid by |
|---|---|---|
| `hookshot_shaders.bin` | the game's shader list, made by the builder from the player's own game (guest code of every stage) | its hash, recorded in `hookshot_prepared.bin` |
| `hookshot_programs.bin` + `.idx` | one record per translated program variant: SPIR-V of each stage, the `Shader::Info` the layout is made from, the head (workgroup, mesh fallback sizes), texture answers | record key = hash of the stages' guest code and the vertex input / colour output types; the whole file by `disk_stamp()` in its header. A different stamp: the file is started again (6-7 GB written again) |
| `hookshot_prepared.bin` | what the last full preparation was made for: stamp, seed hash, seed size and time, program count | stamp differs: "Translator" preparation of every program |
| `hookshot_pipelines.bin` | notes: (program key, variant) plus the draw state of each pipeline the game asked for. No code | `kNoteFormat`, `kPipeKeyVersion`, `sizeof(PipeNote)`, `HOOKSHOT_STATE_STAMP`; a different translator stamp only *carries* the notes (their pipelines are made again) |
| `hookshot_pipeline_states.bin`, `..._shipped.bin` | the pipeline state list: stages' code keys plus state, no SPIR-V; good for any build | its own format; the `.done` file holds the hash of the shipped list already noted |
| `hookshot_computes.bin` | keys of the compute programs the game dispatched | none (guest keys) |
| `mkw_vk_pipelines.bin` | the app's `VkPipelineCache` data | the driver's own header check (vendor, device, `pipelineCacheUUID`); inside, the driver keys each entry on SPIR-V and pipeline state |
| `rxcache/pipelines.bin` | the old wgpu renderer's pipeline descriptors (32 KB); not used by the Vulkan back end | not related to the stamp |

**What a stamp change throws away.** The programs file (all ~36,000 keys, 6-7 GB) and the preparation record. The next start
runs `prepare_run`: every seed program is translated again (graphics programs three times, for float and one and two integer
bone inputs where the program reads them; compute programs once per texture variant, up to 64), and, unless `RX_VK_NO_WARM`,
handed to the driver: two stage libraries per graphics program, a compute pipeline per compute variant (into
`mkw_vk_pipelines.bin`). Then `state_list_prepare` (state list programs and their notes), `predict_run` (up to 90 s of
predicted pipelines built for the driver's own cache), and `precompile_run` (every noted pipeline, built from the app cache).
The notes, the state lists, `hookshot_computes.bin` and `mkw_vk_pipelines.bin` survive; the driver's data in the last one is
good as long as the SPIR-V and the state it was made for come out the same.

## Times on the main PC (Ryzen 7 7800X3D, 16 threads, RTX 5070; a full preparation after a stamp change, RX_VK_SHADER_KEY=0)

| Step | Work (CPU time added up over the workers) | Wall |
|---|---|---|
| Translation of the guest shaders to SPIR-V (31,985 programs, 39,842 variants; `prepare_run`, 8 workers) | 410-580 s | 55-75 s (62 s with the driver part off) |
| The driver's part of `prepare_run` (7,781 compute variants built into the pipeline cache; no stage libraries are built on this GPU) | 80 s | folded into the above (the whole preparation ends at 75-95 s) |
| `predict_run`: 25,575 predicted pipelines built for the driver's cache (driver cache hot) | | 34.6 s (of its 90 s budget) |
| `precompile_run`: the ~6,800 noted pipelines, from the app cache | | 1.4-2 s hot; 71 s when the SPIR-V really changed (cold driver) |

So translation is 85% of the work of a preparation, not "cheap": on a 4-6 core PC it is minutes. The driver's part is the rest.

## What RX_VK_SHADER_KEY does (default on; 0: exactly the old behaviour)

The key of a thing the driver built is a hash of what the driver sees, not the stamp:

* `Program::fp` (vk_shader.cpp `program_fingerprint`): the SPIR-V words of each stage, the stage kinds, the descriptor counts of each of the
  six kinds (what `ensure_layout` makes the set layout from), the push constant range, the flags that make a program push
  (`uses_render_area`, `uses_rescaling_uniform`, `uses_global_memory`), the generic vertex attributes the vertex stage reads, the
  workgroup and shared memory size, the mesh fallback's sizes. Field by field, never a struct's bytes. A program translated now and
  the same one read back from `hookshot_programs.bin` have the same `fp`.
* The ledger `hookshot_compiled.bin` (vk_draw.cpp): 8-byte ids, `fp` mixed with what was built from it (stage libraries; a compute
  pipeline; a graphics pipeline, with `pipeline_key`'s state: topology class, samples, vertex formats, targets, blend and so on).
  Its header holds a hash of what those ids are good for and nothing else: `HOOKSHOT_STATE_STAMP` (vk_state.h, vk_api.h, vk_draw.h,
  rx_nvn.h: the renderer's interface), `kPipeKeyVersion`, vendor, device, driver version, API version, `pipelineCacheUUID`, mesh
  shader mode, which extended-dynamic-state-3 parts the device has, push descriptors. A different header, a file that is not one, or a
  `mkw_vk_pipelines.bin` that did not load (new driver, missing file): the list starts again. A half entry at the end is cut off.
  Entries are written by `ledger_commit` (every 10 s of a preparation, at its end, at the end of the prediction, and every 300 presents
  in play), so a run closed half way keeps what the driver finished.
* What it skips, after the translation (which always runs: the stamp is unchanged): `prepare_run`'s stage libraries and compute
  pipelines, and `predict_run`'s pipelines. `precompile_run` still makes every noted pipeline (it needs the handles), from the app's
  pipeline cache; a hit is 0.2 ms.

What the stamp protected that the fingerprint covers: descriptor layouts (counts), push constants (`kPushSize`, the push flags), vertex
input (`loads`, with the vertex formats in the pipeline id), the specialisation (none is used), the stage set. What stays coarse: the
renderer's interface (`HOOKSHOT_STATE_STAMP`) and the pipeline key's meaning (`kPipeKeyVersion`, bumped by hand), both in the ledger's header.
A change of how `build_graphics` makes its create info that does not change `pipeline_key` needs `kPipeKeyVersion` bumped, as for the notes.

What the driver gives for free: `vkCreate*Pipelines` keys its own cache (and `mkw_vk_pipelines.bin`) on the SPIR-V and the state,
so a rebuild of an unchanged shader is a cache hit, not a compile. What the ledger saves is the calls themselves and the walk:
80 s of work for the compute variants, and the prediction's 25,575 `vkCreateGraphicsPipelines`, 35 s (13 s of it reading the
programs back, which remains). The ledger also covers the case where the driver's cache is *not* hot (a driver that only keeps
its own disk cache): a skipped build is then compiled when first drawn, as before the prediction existed.

Not covered: the translation itself. Nothing knows a program's SPIR-V without translating it, so a translator change still
translates every program (55-75 s here). Making that lazy (translate what the notes name before the start, the rest behind the game)
is the next step; it is `RX_VK_PREPARE_BACKGROUND`'s idea, now without a driver cost behind it.

## Proof (main PC, slot c, RX_VK_SHADER_KEY=1; numbers in the stream report)

Each line is a start in a slot, 110 s at most, so a long preparation is several starts that go on from where the last stopped.

| Case | Driver builds in `prepare_run` | Predicted pipelines (`predict_run`) | Wall of the preparation |
|---|---|---|---|
| Main today, stamp changed (switch 0) | 7,781 compute variants built, 80 s of work | 25,528 built, 34.6 s | 62-95 s |
| Switch on, ledger empty (first run) | 7,741 built and listed, 40 same-SPIR-V repeats left out | 25,528 built, listed | 93 s |
| Stamp changed, nothing in the shaders changed (env stamp change, twice) | 0 built, 7,781 left out | 0 built, 25,575 left out | 75 s (translation only) |
| The real change: Longshot e9d25a36 cherry-picked (translator sources changed, off on NVIDIA) | 0 built, 7,781 left out | 0 built, 25,575 left out (13-25 s reading programs back) | 71 s |
| A change of many shaders (`RX_VK_NAN_GUARD=0`: every fragment output) | compute left out (7,781: they did not change) | the changed graphics pipelines were really compiled (71 s for the 6,809 noted ones, cold driver) | 98 s |
| Cold slot (no caches at all) | everything built (564 s of work in the first 104 s), as before | built | several starts |

Pictures (the slot, switch on, new build): Hyrule citadel in kokiri-idle and a saved zone in hyrule-walk look right; castle-move
only reached its load menu in the run's time (three games ran at once, 160 fps instead of 360).

## K1b: lazy translation on an update (RX_VK_LAZY_UPDATE, on with the key)

K1 skips the driver's part of a preparation, not the translation: 55-75 s of the 62-95 s on the main PC, before the game's first
frame. An update (the translator's stamp changed, everything else as the last start left it) now starts the game at once and does the
work behind it.

**Which start is which** (`prepare_is_update`, vk_draw.cpp):

| Start | What happens |
|---|---|
| First start: no `hookshot_prepared.bin` (missing, empty), or no ledger | Full preparation before the game, with its progress screen, as today: it builds the driver's caches properly. A missing record also drops the ledger (`ledger_drop_all`): what it lists cannot be trusted when the record went with whatever else was wiped |
| A preparation that began and did not end (closed, killed) | `prepared_mark_running` writes the record with stamp 0 while a full preparation runs (it used to remove it). The next start prepares again, goes on from what the program cache holds and the ledger lists, and is **not** a first start, so progress is not thrown away |
| Update: a whole record of another stamp, the pipeline cache (`mkw_vk_pipelines.bin`) loaded, the ledger not empty | **Lazy update** |
| Update, but the pipeline cache is missing, empty, of another device or driver, or did not load | `pcache_init` drops the ledger (K1: the list is not good for a cache that is not this one); the start is a full preparation |
| Update, ledger empty (the last preparation was made before the key, or its list is gone) | Full preparation: nothing says what the driver has |

`RX_VK_LAZY_UPDATE=0`: the old behaviour (a full preparation before the game). `RX_VK_PREPARE_BACKGROUND=1`: the old opt-in, whatever
the caches hold (no driver builds in play, no ledger needed). `RX_VK_SHADER_KEY=0` leaves everything as before K1.

**The lazy update.**
* Before the game: nothing but `precompile_run` (it finds no program for its notes, the program cache having started again under the
  new stamp, and returns at once; the notes are carried to the new stamp as for any update).
* In play: a draw that needs a program not made yet translates it as it always did (on the calling thread; the program is saved to the
  cache and its fingerprint `Program::fp` is computed there). The pipeline is built as a draw builds one: the driver's own cache
  answers a SPIR-V and state it has seen, so a program whose fingerprint did not change costs no driver compile, only the lookup.
  The ledger adds the pipeline's id when it is made, so the walk behind the game skips it.
* Behind the game (`prepare_background`, no 20 s wait: the draws' programs are wanted now), on `clamp(cpus/4, 2, 4)` workers at
  idle priority, in this order:
  1. the programs of the pipeline state lists (the shipped one and the player's), and the variants those lists have by their
     textures: translated into the program cache, not noted and not read in (`state_list_prepare(..., background=true)`; the shipped
     list stays "pending", so the next start notes its pipelines before the game; no layout is made for a program a draw may be
     using, and variants already on disk are not translated twice: `program_variant_kept`);
  2. the rest of the seed (`prepare_run(background=true)`): compute programs first, then graphics. A compute pipeline or stage library
     is handed to the driver only when its ledger id is missing (a changed shader), under the same `.warming` marker as a full
     preparation (a crash in the driver's compiler is not repeated at every start);
  3. `predict_run(background=true)`: the predicted pipelines, each through the ledger: only those whose id is not listed are built,
     on the same few idle workers and with no time budget.
  The record keeps the old stamp until step 2 ends, so a game closed half way starts lazily again and goes on from the program cache;
  the ledger is committed every 30 s, with the pipeline cache saves, and at each step's end.

**What cannot be detected.** The ledger says "built into `mkw_vk_pipelines.bin` and into the driver's cache". What is checked: the
pipeline cache file is there, holds more than its header and is this device's and driver's (`pcache_init`); `hookshot_prepared.bin`
is there and whole. What is not: the driver's own disk cache (NVIDIA's, Mesa's), which a driver cleans by itself or a user empties by
hand, and a `mkw_vk_pipelines.bin` that is the right size and header with its data damaged. In either case a shader the ledger lists as
built is compiled by the driver when first drawn, as before the prediction existed: one stall at that draw, longer for a heavy
pipeline, once (the pipeline cache is saved again with it). Nothing is wrong or missing, only late. Deleting `hookshot_compiled.bin` or
`hookshot_prepared.bin` by hand is the supported way to have everything built again.

**Not measured yet** (the game was not run for this section: the main PC's queue was open and the PC 3050 held by another stream's
whole-machine lock for the whole session): the time to play after a translator change, the frames in play while the work runs behind,
and the background's total time. The code compiles (a syntax pass of `vk_draw.cpp`, `vk_shader.cpp`, `vk_main.cpp` with the
back end's flags), and the record's rules are tested without the game (`tests/test_prepare_seed_stamp.py`: a preparation cut short is
not a first start, a missing or empty record is). What to expect, from K1's numbers: the start no longer waits for the 55-75 s of
translation; the 6,800 noted pipelines are built by the draws that meet them (each a program translated, about 12 ms of CPU on average,
and a pipeline the driver's cache answers) instead of before the game, so the first minutes show hitches where the old start showed a
progress screen; whether they are acceptable is what the measurement is for.

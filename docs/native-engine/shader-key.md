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

## Times on the main PC

(see the end of this file)

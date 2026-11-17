# HeyListen {TAG}

HeyListen turns **your own** Switch 2 copy of *The Legend of Zelda: Ocarina of Time* into a native Windows and Linux
game. The release holds only the **HeyListen Builder**: no game code, data, keys or shaders. You dump your own game as a
`.nup`, the builder reads it, translates and compiles the game on your PC, prepares its shaders, and starts it. See the
[user guide](https://github.com/Sacrappy/HeyListen/blob/main/docs/USER_GUIDE.md).

## Download

- **Windows: HeyListenBuilder.exe**, the setup. Run it; it downloads the builder and its tools (checked by SHA-256) into
  `%LOCALAPPDATA%\HeyListenBuilder` and opens it. Already installed: the builder offers the update itself.
- **Linux: `HeyListen-{TAG}-Linux-x64.AppImage`** (glibc 2.35 and newer; see the Linux section of the user guide).
- **RecompLauncher: `HeyListen-{TAG}-Windows-x64.zip`** (the builder with its tools and `recomplauncher.json`, what the
  launcher installs; about 460 MB) and the AppImage above, which carries the same file. Nothing of the game is in them.
- `heylisten-setup.txt`, `heylisten-linux.txt`: the lists of parts the setup and the AppImage's update read (you do not
  need to download them). `SHA256SUMS.txt`, `SHA256SUMS-linux.txt`: checksums.

**Shaders are prepared again once with this release.** The shaders are translated differently (the mesh programs are
smaller), so the first start after the update prepares them as a new install does. Later updates start at once.

**RecompLauncher**: HeyListen can now be installed and started from [RecompLauncher](https://recomplauncher.com) (the
Windows zip `HeyListen-{TAG}-Windows-x64.zip` and the AppImage carry its `recomplauncher.json`). See "RecompLauncher support"
below.

## New in this release

### RecompLauncher support

- **Setup, play and settings from the launcher**: it builds the game from your own `.nup` with no window (the same build as
  the builder's), starts it, shows the in-game settings and finds your saves. NVIDIA DLSS (with NVIDIA's own runtime, fetched by
  the launcher: nothing of NVIDIA's or AMD's is in this release) and AMD FSR 4 are ticks on its setup screen.
- **A `logs` folder** in the game folder keeps the last five runs' logs for the launcher's crash reports (`hookshot_log.txt`
  stays as before).

### Fixes

- **AMD lighting** (#45): storage buffers are bound and translated at the driver's 16-byte alignment, which removes wrong
  lighting on Radeon GPUs.
- **Older game folders start again** (#48): a folder built by an older version no longer aborts in the shader preparation.
- **Above 60 fps** (#46): sounds, moving platforms, the pick-up counter, the time of day, water ripples, the Deku Tree's web and
  the opening movie's cues keep their speed above a 60 fps cap.
- **Quarter-second frames** (#56): no virtual core is held while a thread sleeps on a host lock, nor through file writes,
  texel copies or command flushes; a thread starved of its core gets it back after 1 second.
- **Fixes taken from Longshot**: Intel (compile faults, Arc B-series device loss, integrated GPUs, weak laptops), AMD (ASTC
  decoded on the CPU for the graphics in AMD's desktop CPUs, which ended GPU resets in Kokiri Forest), the Steam Deck's Game
  Mode (no recompiling of every pipeline), shader programs over 1 MiB, samplers past the driver's limit, read-backs after a
  timeout and guest pages under an import.
- **AMD RX 6000 and the Steam Deck on Linux** (#51): mesh work runs on the compute path by default (RDNA 2 only), the fix for device-lost crashes.
- **RX 6700 / 6800 on Windows** (#57): task and mesh draws run on the compute path on Navi 21, 22 and 23; the foliage and the far trees draw correctly there.
- `RX_VK_MESH_SHADERS=native` or `compute` chooses the path by hand (NVIDIA and the other GPUs keep the native one by default).
- **Linux**: the sound output is closed before it is opened again (#44); short sleeps wait on the timer (#43).
- A stuck GPU's stall report says what it was running; `hookshot_env.txt` takes the hybrid engine's switches.

### Performance

- **Smaller mesh shaders**: mesh and task programs over 30,000 instructions keep their global-access helpers out of line, constant
  flag-carrying operations fold, and each mesh invocation works its buffer ranges out once. Compiles are faster and the programs are
  smaller on every GPU.
- **Upstream Hookshot's performance batch**, credited in the README: background workers on PCs with 16 threads or more, texture
  spans aged once a frame, fewer copies and write-backs in the replay, a batched log, the pipeline cache written only when it
  changed, and passes ordered by the game's own barriers (NVIDIA).
- **Fewer stalls**: a thread's core is given back when a memory region is freed or an escape ends; a draw binds only the
  vertex streams that changed; flat per-draw caches in the replay; recording threads wait less on the copy lock (#38 to #42).
- **Faster program cache**: its records are packed (6.4 GB became 1.7 GB).

### Opt-in telemetry

Two **Privacy** settings, both **off** by default: *Send crash reports* (a crash window offers the report) and *Send performance
statistics*. They send nothing until you turn them on, and the report holds your PC's part names and the game's log, with your
user name and paths removed; no save, no game data, no account. The menu says what is sent. Turning a setting off stops it at once.
Details: [the user guide](https://github.com/Sacrappy/HeyListen/blob/main/docs/USER_GUIDE.md#privacy-crash-reports-and-performance-statistics).


# Performance statistics and crash reports

HeyListen can send two things to the team, and only when the player turns them on in the game's menu (Insert menu,
Gameplay tab). Both are **off by default**, and both say **Not available in this build** until the build has an address to
send to (`nx2/addresses.ini`, `telemetry=`, which is empty as shipped).

| Row | What it does | Default |
|---|---|---|
| **Send performance statistics** | The game counts how its frames went and sends the counts, at the next start. The same row sends the new pipeline states the game made (hashes and render state, see *Pipeline states* below), unless `HL_TELEMETRY_PIPELINES=0` | Off |
| **Send crash reports automatically** | After a crash, the crash report is sent at once, without the crash window asking | Off (the window asks) |

Ported from Longshot's pull request #983 (Gabriel Moisan Matte), which builds on its crash window (#593, HumanBean) and its
crash records (#75, mooki). Longshot's payload formats are kept as they are: HeyListen's server validates the same field
tables.

## What the player sees

- The two rows, on the Gameplay tab. They are saved with the other options in `hookshot_options.ini`, as
  `send_performance_stats=` and `send_crash_reports=` (0 or 1; anything but 1 is off).
- `perf_pending/` in the game folder: the summaries waiting to be sent. `perf_sent/`: each summary the server took,
  exactly as it was sent (the 48 sent last), for a player to open and read. Turning **Send performance statistics** off
  deletes both folders' summaries at once. `pipelines_pending/` and `pipelines_sent/` are the same for the pipeline state
  records (see below), deleted together with them.
- After a crash, the crash window (the game's own, drawn like the Insert menu) says what happened, and offers **Report
  automatically** (shows the report that would be sent, every line of it, and sends it only when Send is pressed),
  **Report manually** (copies a redacted report to the clipboard and opens a new issue on GitHub with a title and a short
  body, for the player to paste into), **Open the log folder** and **Close**. Report automatically is not offered when the
  build has no address or the PC has no `curl` (Windows 10 and later ship `curl.exe`; Linux needs `curl`).
- The log says `vk: performance statistics on (...)` when the row is on and the build has an address.

## What is sent

Everything goes to `https://<the build's address>`, with no account, ID or path, with the system's `curl`, over https only,
no redirect followed, and nothing is kept per player.

| Endpoint | When | Needs | Becomes |
|---|---|---|---|
| `POST /telemetry/crash` | The player presses Send in the crash window, or after a crash with **Send crash reports automatically** on | The player's click or the row | A report per crash |
| `POST /telemetry/perf` | Every 5 minutes of play and at exit, sent at the next start | **Send performance statistics** on | Totals for all players; nothing kept per player |
| `POST /telemetry/pipelines` | At exit the game queues the pipeline states it made that the shipped list lacks; sent at the next start | **Send performance statistics** on, and `HL_TELEMETRY_PIPELINES` not `0` | The records the next shipped pipeline state list is made from; nothing kept per player |

All bodies are `Content-Type: application/json`. The server answers 2xx for a taken report, 4xx with `{"error": ...}` for a
refused one, 429 or 5xx when busy.

### Crash report (`/telemetry/crash`, at most 128 KiB)

| Field | Type | Required | Limits | What it is |
|---|---|---|---|---|
| `crash` | array of string | yes |  | The crash handler's lines; the first is `CRASH: exception <code> (<read\|write> ...) at <module+offset>`. The rows of the game's memory (registers and what they point to) are left out |
| `version` | string | yes | ≤ 40 characters | The game's version |
| `cpu` | string |  | ≤ 200 characters | The CPU, its threads and the RAM |
| `gpu` | string |  | ≤ 200 characters | The GPU |
| `log_tail` | array of string |  |  | The log's last 150 lines before the crash |
| `map` | string |  | ≤ 120 characters | The map played when it crashed: the scene and Link's age (or "Preparing shaders", "Title and intro") |
| `options` | string |  | ≤ 4000 characters | The game's settings, one per line, a value only when it is a number, a size or on/off |
| `os` | string |  | ≤ 200 characters | The operating system and its version |
| `release` | boolean |  |  | From an actual release (the builder's signed source fingerprint equals the log's); anything else is not |
| `renderer` | string |  | ≤ 120 characters | The renderer |

Example:

```json
{"version": "v0.4.0", "release": true, "os": "Windows 11 (build 26200)", "renderer": "Vulkan",
 "gpu": "NVIDIA GeForce RTX 3050, NVIDIA 566.36", "cpu": "AMD Ryzen 5 5600 (12 threads), 32 GB",
 "map": "Kokiri Forest (child)",
 "crash": ["CRASH: exception 0xc0000005 (read 0x8, guest 0x8) at HeyListen.exe+0x1a2b3c", "CRASH: thread Main, sp 0x1000"],
 "log_tail": ["vk: status: 3.1 of 15.9 GB"], "options": "upscaling=1\nquality_preset=3"}
```

A crash before the renderer has written its `report:` log lines (a crash at the very start) has no `os`, `cpu` or `gpu` from the log: the helper reads the CPU (with its threads and RAM) and the operating system itself and sends them as `cpu` and `os`, `gpu` is the adapters the log had listed, and `renderer`, which nothing says yet, is left out rather than sent empty.

Privacy: every line is cleaned before it is shown or sent. The home folder is written `~` (or `%USERPROFILE%` on Windows), the
user name `<user>` (also as a word of its own), the audio and microphone device names `<device>`, control characters are
dropped, a line is cut at 300 characters, and a setting's value is sent only when it is a number, a size or on/off. The log
tail is the game's own log (no save data, no game files, no keys, no memory dump). `tests/test_crash_dialog.py` checks it.

### Performance summary (`/telemetry/perf`, at most 60 KiB)

| Field | Type | Required | Limits | What it is |
|---|---|---|---|---|
| `v` | integer | yes |  | The format's version: 1 |
| `version` | string | yes | ≤ 40 characters | The game's version |
| `cpu` | string |  | ≤ 120 characters | The CPU |
| `dlss_model` | string |  | ≤ 40 characters | The network DLSS draws with, by its preset's letter (`K`, `M`, `L`, `E`, ...), `default` (NVIDIA's own pick) or `none` while DLSS or DLAA is not what upscales |
| `driver` | string |  | ≤ 60 characters | The GPU driver's version |
| `first` | boolean |  |  | The session's first summary |
| `fps_cap` | integer |  | ≥ 0 | The frame rate cap in effect |
| `framegen` | string |  | ≤ 40 characters | What makes the extra frames: `off`, `builtin` (the renderer's own interpolation), `fsr3` (AMD FSR 3.1's frame interpolation, at 2x where it runs) or `dlssg` (NVIDIA DLSS FG, the Frame generation by row's choice where its runtime runs and a temporal Upscaler's inputs are there) |
| `framegen_auto` | boolean |  |  | The Frame generation row is Auto (a frame for each of the display's refreshes): `framegen_x` is then the most frames it gives a game frame |
| `framegen_x` | integer |  | 1 to 8 | The frames shown for each of the game's: 1 with `framegen` `off`, 2 to 8 for a fixed multiplier, 2 to 4 with `framegen_auto` |
| `gpu` | string |  | ≤ 120 characters | The GPU |
| `kernel` | string |  | ≤ 60 characters | The kernel's version (Windows: `10.0.<build>`) |
| `map` | string |  | ≤ 120 characters | The map played when the summary was made |
| `maps` | object of MapSummary by key |  | at most 16 keys | The maps played since the last summary, by name |
| `memory` | object |  |  | `vram_used_gb`, `vram_allowed_gb`, `ram_used_gb`: a sample |
| `os` | string |  | ≤ 60 characters | The operating system |
| `preset` | string |  | ≤ 40 characters | The graphics preset |
| `ram_gb` | number |  | ≥ 0 | The PC's memory, GB |
| `release` | boolean |  |  | A release's version built from the release's own sources |
| `render_scale` | integer |  | 0 to 100 | The share of the output the 3D is drawn at, in percent (the Render resolution row: 100, 75, 67, 58, 50 or 33); 100 with DLAA or FSR 4's native AA; 0 for Auto, the game's own dynamic resolution |
| `renderer` | string |  | ≤ 40 characters | The renderer |
| `session_s` | number |  | ≥ 0 | Seconds since the session started |
| `upscaler` | string |  | ≤ 40 characters | The upscaler in use |
| `vram_gb` | number |  | ≥ 0 | The GPU's memory, GB |

The settings in a summary are read when the summary is made, so a summary says what the player used at its end. `upscaler` is
the Upscaler row's choice (`Game network`, `DLSS`, `DLAA`, `FSR 3.1`, `FSR 4`, `FSR 4 AA`, or `other` for a value past the
row); `framegen`, `framegen_x`, `framegen_auto`, `dlss_model` and `render_scale` are what runs: a choice this PC cannot run
(DLSS without its runtime, frame generation with its images not made or `RX_VK_FRAMEGEN=0`) is reported as it behaves, with
`framegen` `off`, `framegen_x` 1 and `dlss_model` `none`. `framegen_x` comes from the multiplier the renderer makes its frames
for (the row's, or Auto's limit from the display's refresh rate), not from a count of the frames shown; with `framegen_auto` the
real multiplier is between 1 and `framegen_x`, by how the game's rate fits the display's.

Per map (`MapSummary`):

| Field | Type | What it is |
|---|---|---|
| `new` | boolean | First summary of this map in the session |
| `seconds` | number | Seconds played on it |
| `frame_ms` | array of 13 numbers | Frame counts: up to 4.17, 6.94, 8.33, 11.1, 16.7, 20, 25, 33.3, 50, 100, 250, 1000 ms, then the rest |
| `frame_ms_sum` | number | The frame times' sum, ms |
| `gpu_ms` | array of 11 numbers | Frame counts by the GPU's own time: up to 2, 4, 6, 8.33, 11.1, 16.7, 25, 33.3, 50, 100 ms, then the rest |
| `gpu_ms_sum` | number | The GPU times' sum, ms |
| `parts_ms` | object | `game`, `render`, `gpu_wait`, `pace`: **0 in HeyListen** (its overlay does not measure a frame's parts) |
| `hitches` | object | Frames over twice the longer of their target and the recent average (and at least 25 ms), by what they waited on most: `pipeline_wait`, `shader_translate`, `texture_upload`, `memsync`, `gpu_bound`, `readback` (0: not measured), `game_cpu` and `pace` (0: not measured), `other` |
| `pacing_misses` | number | Frames more than a quarter of the target off the one before |
| `draws_left_out` | number | **0 in HeyListen** (not counted) |
| `events` | object | `program_failed`, `device_lost`, `swapchain_recreated`, `movie_upscaler_dropped`, `dlss_recreated`, `vram_over_budget` |
| `movie` | object | `shown` (pictures the game took), `dropped` (skipped to keep up with the sound), `late`, `decode_ms`, `fallbacks`: the last three are 0 in HeyListen |

Example:

```json
{"v": 1, "version": "v0.4.0", "release": true, "first": true, "session_s": 312,
 "os": "Windows 11", "kernel": "10.0.26200", "renderer": "Vulkan",
 "gpu": "NVIDIA GeForce RTX 3050", "driver": "NVIDIA 566.36", "vram_gb": 8, "cpu": "AMD Ryzen 5 5600", "ram_gb": 32,
 "upscaler": "DLSS", "preset": "Balanced", "fps_cap": 60,
 "framegen": "fsr3", "framegen_x": 2, "framegen_auto": false, "dlss_model": "K", "render_scale": 67,
 "map": "Kokiri Forest (child)",
 "memory": {"vram_used_gb": 5.2, "vram_allowed_gb": 7.1, "ram_used_gb": 7.9},
 "maps": {"Kokiri Forest (child)": {"new": true, "seconds": 250,
   "frame_ms": [0, 0, 0, 0, 14800, 120, 40, 10, 4, 1, 0, 0, 0], "frame_ms_sum": 250010.5,
   "gpu_ms": [0, 2100, 9800, 2900, 150, 25, 0, 0, 0, 0, 0], "gpu_ms_sum": 81200,
   "parts_ms": {"game": 0, "render": 0, "gpu_wait": 0, "pace": 0},
   "hitches": {"pipeline_wait": 3, "texture_upload": 1}, "pacing_misses": 12, "draws_left_out": 0,
   "events": {"swapchain_recreated": 1},
   "movie": {"shown": 0, "dropped": 0, "late": 0, "decode_ms": 0, "fallbacks": 0}}}}
```

Never sent: paths, the user name, settings beyond the graphics ones above (the upscaler, the preset, the frame rate cap, frame generation and its multiplier, the DLSS model and the render resolution), the log,
saves, anything from the game files. A summary carries no ID, so two summaries can't be tied to one player.

### Pipeline states (`/telemetry/pipelines`, at most 1.5 MiB)

The game keeps a list of the graphics pipelines it has asked for (`hookshot_pipeline_states.bin`, the same format as the shipped
list `profile/pipeline_states.bin`, installed as `hookshot_pipeline_states_shipped.bin`). A record is 528 bytes of hashes of the
game's shader stages, texture and vertex formats and draw-state numbers: **no shader code, no game data, no title id, no path**
(`tools/release/check_states.py` is the rule for a record and the game applies it to each one before it is queued). At exit
(and when the window closes) the game takes the records that are **not in the shipped list**, once each, and leaves out the ones
it queued before (`hookshot_pipeline_states_sent.bin` keeps their hashes). At most 1,800 a session (under 1 MB); the rest
wait for the next session. With nothing new, or no shipped list to compare with, nothing is queued.

| Field | Type | Limits | What it is |
|---|---|---|---|
| `v` | integer | | The format's version: 1 |
| `version` | string | ≤ 40 characters | The game's version, as the performance summary says it |
| `release` | boolean | | A release's version built from the release's own sources |
| `state_stamp` | string | 16 hex digits | `HOOKSHOT_STATE_STAMP`: the hash of the renderer's state headers (what the records are laid out for) |
| `shader_stamp` | string | 16 hex digits | The shader translation's stamp (`program_cache_stamp`: the translator, the depth mode, the GPU's mesh-shader mode, the C++ library) |
| `vendor_id` | integer | | The GPU's PCI vendor id (4318 NVIDIA, 4098 AMD, 32902 Intel) |
| `driver` | string | ≤ 60 characters | The GPU driver as the performance summary's `driver` says it; the server derives the class |
| `record_size` | integer | | 528 |
| `count` | integer | 1 to 1800 | The records in `states` |
| `states` | string | base64 | A state list file, as `nx2/pipeline_states.py` and `tools/release/check_states.py` read it: `HKSTATE1`, a little-endian u32 528, then `count` records. Decoded it can be written to disk and merged as it is |

Example (the records cut short):

```json
{"v": 1, "version": "v0.4.0", "release": true, "state_stamp": "0123456789abcdef", "shader_stamp": "fedcba9876543210",
 "vendor_id": 4318, "driver": "NVIDIA 566.36", "record_size": 528, "count": 2, "states": "SEtTVEFURTEQAgAA..."}
```

Never sent: the GPU's name or device id, the CPU, the OS, the PC's name or folders, the user name, any ID. The file in
`pipelines_sent/` is the exact body of each batch sent (the last 3). `tests/test_state_report.py` checks the payload against
this PC's own names and folders.

## How the parts fit

- **The game** (`gpu/vk/perf_report.cpp`, `perf_report.h`): while the row is on and the build has an address, each frame
  shown (`menu_note_frame`) is added to its map's totals (a few array increments, 5 ns). Every 5 minutes of play
  (`RX_VK_PERF_INTERVAL`, 60 to 840 s) a summary is made under a lock and written on a thread of its own to
  `perf_pending/<unix time>-<n>.json`; the last is written when the window closes (`mkwvk_close_asked`) or the device is
  lost. A summary holds 16 maps at most; at most 48 summaries wait (past that the game writes none). The map and the movies'
  totals come from the host (`hos_perf_info`, `MkwVkHost::perf_info`, API version 14). Turning the row off stops all of it and
  deletes `perf_pending/` and `perf_sent/`. Off, the cost is a look at the row every 64th frame.
- **The sender** (`hos_perf_send_run` in `host/hos_crash_dialog.c`, rules in `host/hos_perf_send.h`): the game ships no
  Python, so Longshot's `nx2/perf_report.py` is ported to C. At the game's start, on a thread of its own, with the row on and an
  address, it posts what waits in `perf_pending/`, oldest first, one request each with the system's `curl` (10 s limit), at most
  12 a start. A summary the server took or refused is deleted (one it took is kept in `perf_sent/`); one it could not reach, or
  answered busy (429 or 5xx), stays. A busy server is not asked again before its `Retry-After` (at most a day), else an hour,
  doubled each time it is busy again up to a day: the time is kept in `perf_pending/.next`.
- **The pipeline states** (`gpu/vk/state_report.h` the rules, `perf_report.cpp` `states_queue` the queuing): at the window's close
  and at exit, with the row on, an address and `HL_TELEMETRY_PIPELINES` not `0`, the batch is written whole to
  `pipelines_pending/<unix time>-0.json` (at most 4 wait; past that nothing is queued) and its records' hashes are added to
  `hookshot_pipeline_states_sent.bin`. The sender (`hos_pipes_send_run`, the same code as the summaries' with its own folders)
  posts at most 2 a start, with a 1.5 MiB limit, keeps the last 3 sent in `pipelines_sent/` and has its own `.next` backoff. The
  log says `vk: pipeline states: N new records queued ...` or `... nothing queued to send: <why>`, and at the next start
  `perf: N pipeline state batches sent or refused`. Turning the row off deletes `pipelines_pending/`, `pipelines_sent/` and the
  sent hashes.
- **The address** (`host/hos_telemetry.h`): `nx2/addresses.ini`'s `telemetry=`, stamped by `nx2/make_game.py` into the game
  folder's `hookshot_version.txt`. Empty (the default): nothing is stamped, nothing can be sent, and both rows say **Not
  available in this build**.

## Switches

| Switch | Default | What it does |
|---|---|---|
| `HL_TELEMETRY_URL` | the build's address | Where reports go, for a test against a local server. From the real environment only (`hookshot_env.txt` is refused it). https only |
| `HL_TELEMETRY_TEST_HTTP=1` | off | With `HL_TELEMETRY_URL`, also takes a plain `http://` address on this PC itself (`127.0.0.1`, `localhost`, `[::1]`). For tests only |
| `HL_TELEMETRY_PIPELINES=0` | on (with the row) | Turns off only the pipeline state records: none is queued or sent, the performance summaries go as the row says |
| `RX_VK_PERF_INTERVAL` | `300` s | Seconds of play between two summaries (60 to 840) |
| `HL_PERF_MAX_SENDS` | `12` | Summaries sent at one start (1 to 48) |
| `HL_PERF_TIMEOUT` | `10` s | A request's time limit (2 to 60) |
| `HL_PERF_BACKOFF=0` | on | Asks a busy server at every start instead of leaving it alone |
| `HOS_CRASH_TEST=start` or `<seconds>` | off | The game crashes on purpose, to see the crash window |
| `HOS_CRASH_WATCH=0` | on | No crash window (the crash is still recorded in `bug-reports/pending`) |

## Testing against a local server

A server that prints what it gets (not committed; ten lines of Python's `http.server`) on `127.0.0.1:8099`, then:

```
set HL_TELEMETRY_URL=http://127.0.0.1:8099
set HL_TELEMETRY_TEST_HTTP=1
set RX_VK_PERF_INTERVAL=60
HeyListen.exe
```

with **Send performance statistics** on in the menu: a summary is written to `perf_pending/` after a minute of play and
sent at the next start. `HOS_CRASH_TEST=start` shows the crash window; Report automatically then shows and posts the report.
`tests/test_telemetry.py` and `tests/test_crash_dialog.py` run the same paths against a stand-in for `curl`.

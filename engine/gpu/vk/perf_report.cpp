// (heylisten) The performance summary (perf_report.h), ported from Longshot's #983: on only when the player chose "Send
// performance statistics" in the menu (opts.send_perf, hookshot_options.ini's send_performance_stats=, OFF by default) AND the
// build has an address to send to (nx2/addresses.ini's telemetry=, stamped into hookshot_version.txt, or HL_TELEMETRY_URL for a
// test; empty: nothing is measured or written). Each frame shown is added here, on the thread that presents, to its map's
// totals: a bucket, a few sums and the renderer's counters read since the frame before. No allocation, no file and no lookup a
// frame: the map is asked of the host about once a second, and a summary's JSON is made and written on a thread of its own every
// `period` seconds (RX_VK_PERF_INTERVAL) and at exit into the game folder's perf_pending/, which host/hos_crash_dialog.c's
// hos_perf_send_run sends at the next start. Turning the row off deletes perf_pending/ and perf_sent/.
// The same row also lets the pipeline state records the player's game made that the shipped list lacks go (state_report.h): at the
// session's end they are queued into pipelines_pending/, which hos_pipes_send_run sends at the next start to /telemetry/pipelines.
// HL_TELEMETRY_PIPELINES=0 turns that part off alone.
// What HeyListen's overlay does not measure (the frame's parts: game, render, GPU wait, pacing; the draws left out; a read-back's
// wait) is sent as 0, so the payload keeps Longshot's field tables, which the server validates.
#include "perf_report.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "../../host/hos_telemetry.h"
#include "report_info.h"
#include "state_report.h"
#include "vk_draw.h"
#include "vk_internal.h"
#include "vk_menu.h"
#include "vk_paths.h"
#include "vk_shader.h"
#include "upscaler_rows.h"

#ifdef _WIN32
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace mkwvk {

bool shaders_preparing();   // (vk_main.cpp: the start-up screen's shader preparation)

namespace {

namespace pr = perf_report;
namespace fs = fps_stats;
namespace fsys = std::filesystem;

constexpr double kMapEvery = 1;      // (seconds between asking the host where Link is)
constexpr int kMaxPending = 48;      // (summaries waiting in perf_pending at most, about four hours of play: the newest go unwritten)

std::atomic<bool> perf_measure{false};   // (on: the row is on and the build has an address)
std::mutex mu;   // (the presenting thread's frames, against the exit's and a lost device's last summary)
std::map<std::string, pr::MapTotals> maps;
std::map<std::string, bool> seen_maps;   // (maps this session has sent: the server counts a session on a map once)
pr::MapTotals* current = nullptr;
std::string current_map;
pr::Machine machine;
bool machine_read = false;
fsys::path folder;
std::atomic<int64_t> events[pr::kEvents];
int64_t started_ns = 0, last_movie[5] = {}, last_frame_ns = 0, l_gpu = 0;
double period = pr::kPeriodDefault;
double since_map = 0, since_summary = 0, avg_ms = 0, last_ms = 0;
bool first = true;
int seq = 0;
int64_t l_failed = 0;
bool primed = false;

int64_t clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// The build's address, or HL_TELEMETRY_URL's: "" without one (host/hos_telemetry.h's rule)
std::string address_now() {
    std::string stamped;
    {
        std::ifstream f(game_dir_path() / "hookshot_version.txt", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        char value[HOS_TELEMETRY_URL_MAX + 8] = "";
        hos_telemetry_stamp_value(text.c_str(), "telemetry", value, sizeof value);
        stamped = value;
    }
    char base[HOS_TELEMETRY_URL_MAX + 8];
    return hos_telemetry_base(getenv(HOS_TELEMETRY_ENV), getenv(HOS_TELEMETRY_TEST_HTTP_ENV), stamped.c_str(), base, sizeof base)
               ? base
               : "";
}

double ram_used_gb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pm{};
    pm.cb = sizeof pm;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm)) return pm.PrivateUsage / 1073741824.0;
#else
    FILE* f = fopen("/proc/self/statm", "r");
    long pages = 0, resident = 0;
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        fclose(f);
        return resident * (double)sysconf(_SC_PAGE_SIZE) / 1073741824.0;
    }
#endif
    return 0;
}

double vram_total_gb() {
    if (!dev.physical) return 0;
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(dev.physical, &mp);
    uint64_t most = 0;
    for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
        if (mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) most = std::max<uint64_t>(most, mp.memoryHeaps[i].size);
    return most / 1073741824.0;
}

const char* upscaler_name(int v) {
    static const char* const t[] = {"Game network", "DLSS", "DLAA", "FSR 3.1", "FSR 4", "FSR 4 AA", "Off"};
    static_assert(sizeof t / sizeof t[0] == upscaler_rows::kUpscalers, "a name for each upscaler");
    return v >= 0 && v < upscaler_rows::kUpscalers ? t[v] : "other";
}
const char* preset_name(int v) {
    static const char* const t[] = {"Custom", "Minimum", "Low", "Balanced", "High", "Ultra"};
    return v >= 0 && v < 6 ? t[v] : "Auto";
}
/// The Frame rate cap row's value (host/nn_vi.c: 0 60, 90, 120, 144, 165, 240), HOS_FPS_MAX over it
int fps_cap_now() {
    static const int env_max = getenv("HOS_FPS_MAX") ? std::max(1, atoi(getenv("HOS_FPS_MAX"))) : 0;
    if (env_max) return env_max;
    static const int caps[] = {60, 90, 120, 144, 165, 240};
    return opts.fps_cap >= 0 && opts.fps_cap < 6 ? caps[opts.fps_cap] : 60;
}

/// The machine, once: the GPU and its driver from Vulkan, the rest from report_info's words
void read_machine(const char* version) {
    machine.version = version ? version : "";
    {   // (hookshot_version.txt's source= against the log's: the release flag)
        std::ifstream f(game_dir_path() / "hookshot_version.txt", std::ios::binary);
        std::string stamp((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        machine.release = pr::release_flag(machine.version, stamp);
    }
    machine.version = machine.version.substr(0, machine.version.find(" ("));   // ("<version> (source <hash>)": the version)
    machine.gpu = dev.props.deviceName;
    machine.driver = report_info::driver_text();
    machine.vram_gb = vram_total_gb();
    const std::string cpu = report_info::cpu_text();   // ("<cpu> (<n> threads), <ram> GB")
    const size_t threads = cpu.find(" ("), gb = cpu.find(" GB");
    machine.cpu = cpu.substr(0, threads);
    if (gb != std::string::npos) {
        const size_t from = cpu.rfind(", ", gb);
        machine.ram_gb = atof(cpu.c_str() + (from == std::string::npos ? 0 : from + 2));
    }
    const std::string os = report_info::os_text();
    machine.os = os.substr(0, os.find(" ("));
    machine.kernel = pr::kernel_of(os);
    machine_read = true;
}

void read_settings() {
    machine.upscaler = upscaler_name(opts.dlss);
    machine.preset = preset_name(opts.preset);
    machine.fps_cap = fps_cap_now();
}

/// The map from the host, and the movies' totals since the last ask, onto the map they played on. True when Link is on a map
/// the summary has no room for (pr::kMaxMaps): it stays on the one before, for a summary to make room
bool where_now() {
    char map[120] = "";
    int64_t movie[5] = {};
    const char* version = host->perf_info ? host->perf_info(map, sizeof map, movie) : nullptr;
    if (!machine_read) read_machine(version);
    // (before a map: the start-up screen while the shaders are prepared, then the intro, the title and the file select)
    const std::string name = map[0] ? map : shaders_preparing() ? pr::kPreparingShaders : pr::kTitleAndIntro;
    const bool full = current && name != current_map && !maps.count(name) && maps.size() >= (size_t)pr::kMaxMaps;
    if (!full && (!current || name != current_map)) {
        current_map = name;
        current = &maps[name];
        current->fresh = !seen_maps.count(name);
    }
    current->movie_shown += movie[0] - last_movie[0];
    current->movie_dropped += movie[1] - last_movie[1];
    current->movie_late += movie[2] - last_movie[2];
    current->movie_decode_ms += (movie[3] - last_movie[3]) / 1e6;
    current->movie_fallbacks += movie[4] - last_movie[4];
    memcpy(last_movie, movie, sizeof last_movie);
    return full;
}

/// A report into dir/<sub>/<name>.json, whole or nothing; false when `limit` are waiting there already or it could not be written
bool write_summary(const fsys::path& dir, const std::string& name, const std::string& json, const char* sub = "perf_pending",
                   int limit = kMaxPending) {
    const fsys::path pending = dir / sub;
    std::error_code ec;
    {
        int waiting = 0;
        for (fsys::directory_iterator it(pending, ec), end; !ec && it != end; it.increment(ec))
            waiting += it->path().extension() == ".json";
        if (waiting >= limit) return false;
    }
    fsys::create_directory(pending, ec);
    const fsys::path tmp = pending / (name + ".tmp"), path = pending / (name + ".json");
    bool ok;
    {
        std::ofstream f(tmp, std::ios::binary);
        ok = f && f.write(json.data(), (std::streamsize)json.size()) && f.flush();
    }
    if (ok) fsys::rename(tmp, path, ec);
    if (!ok || ec) fsys::remove(tmp, ec);   // (whole or nothing: the sender never reads half)
    return ok && !ec;
}

constexpr int kMaxPipelinesPending = 4;   // (batches waiting in pipelines_pending at most: host/hos_perf_send.h HOS_PIPES_MAX_PENDING)
std::mutex states_mu;
int states_runs = 0;
uint64_t state_stamp = 0, shader_stamp = 0;
bool stamps_read = false;

/// A file's bytes up to `limit` (none when it is not there)
std::string read_file(const fsys::path& p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    std::string s;
    if (!f) return s;
    s.resize(limit);
    f.read(s.data(), (std::streamsize)limit);
    s.resize((size_t)f.gcount());
    return s;
}

/// The two stamps the records are sent with, read once (the shader stamp asks the host: not at the very end of the process)
void read_stamps() {
    if (stamps_read) return;
    stamps_read = true;
    state_stamp = pipeline_state_stamp(), shader_stamp = program_cache_stamp();
}

/// The session's end (mu held): the records this player's game made that the shipped list lacks go into pipelines_pending/ for the
/// next start to send, and are remembered in hookshot_pipeline_states_sent.bin. Says in the log how many, or why not. Needs the
/// performance statistics row, an address and not HL_TELEMETRY_PIPELINES=0; at most state_report::kMaxRecords a session
void states_queue() {
    namespace sr = state_report;
    std::unique_lock lk(states_mu, std::try_to_lock);   // (the window's close and the exit may both come here: the second finds nothing new)
    if (!lk) return;
    const bool first = states_runs++ == 0;
    const char* why = sr::why_not(opts.send_perf == 1, telemetry_available(), getenv(HOS_TELEMETRY_PIPELINES_ENV));
    const fsys::path dir = game_dir_path();
    std::string list, shipped_bytes;
    std::unordered_set<uint64_t> shipped;
    if (!*why) {
        shipped_bytes = read_file(dir / sr::kShippedFile, 64u << 20);
        shipped = sr::list_hashes(shipped_bytes);
        list = read_file(dir / sr::kListFile, 64u << 20);
        if (shipped.empty()) why = "no shipped pipeline state list to tell new records from";
        else if (sr::list_records(list).empty()) why = "this session recorded no pipeline states";
    }
    if (*why) {
        if (first) logf("vk: pipeline states: nothing queued to send: %s", why);
        return;
    }
    bool valid = false;
    const std::string sent_bytes = read_file(dir / sr::kSentFile, sr::kMaxSentFile);
    const std::unordered_set<uint64_t> sent = sr::sent_decode(sent_bytes, &valid);
    const sr::Batch batch = sr::select_new(list, shipped, sent);
    if (!batch.count) {
        if (first) logf("vk: pipeline states: 0 new records (%zu in the shipped list, %zu sent before, %zu not fit to send)", batch.in_shipped, batch.already_sent, batch.refused);
        return;
    }
    if (!machine_read) {
        char map[120] = "";
        int64_t movie[5] = {};
        read_machine(host->perf_info ? host->perf_info(map, sizeof map, movie) : nullptr);
    }
    read_stamps();
    sr::Meta meta;
    meta.version = machine.version, meta.release = machine.release, meta.driver = machine.driver;
    meta.vendor_id = dev.props.vendorID, meta.state_stamp = state_stamp, meta.shader_stamp = shader_stamp;
    char name[48];
    snprintf(name, sizeof name, "%lld-0", (long long)std::time(nullptr));
    if (!write_summary(dir, name, sr::payload_json(meta, batch), "pipelines_pending", kMaxPipelinesPending)) {
        logf("vk: pipeline states: %zu new records not queued: %d batches are waiting to be sent already, or the file could not be written", batch.count, kMaxPipelinesPending);
        return;
    }
    {   // (remembered once queued: the same records are never queued again)
        std::ofstream f(dir / sr::kSentFile, std::ios::binary | (valid ? std::ios::app : std::ios::trunc));
        const std::string add = sr::sent_encode(batch.hashes, !valid);
        f.write(add.data(), (std::streamsize)add.size());
    }
    logf("vk: pipeline states: %zu new records queued in pipelines_pending, sent at the next start (%zu in the shipped list, %zu sent before, %zu left for the next session)",
         batch.count, batch.in_shipped, batch.already_sent, batch.over_cap);
}

/// The player turned the row off (or the address went): what is waiting and what was kept goes, nothing more is measured
void switch_off() {
    perf_measure.store(false, std::memory_order_relaxed);
    maps.clear(), current = nullptr, primed = false, last_frame_ns = 0;
    std::error_code ec;
    for (const char* sub : {"perf_pending", "perf_sent", "pipelines_pending", "pipelines_sent"}) {
        for (fsys::directory_iterator it(folder / sub, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string ext = it->path().extension().string(), leaf = it->path().filename().string();
            if (ext == ".json" || ext == ".tmp" || leaf == ".next" || leaf.rfind(".sending.", 0) == 0) fsys::remove(it->path(), ec);
        }
    }
    // (the pipeline records deleted before they were sent must not stay marked as sent)
    fsys::remove(folder / state_report::kSentFile, ec);
    logf("vk: performance statistics off: what was waiting in perf_pending and perf_sent (and the pipeline state records in pipelines_pending and pipelines_sent) is deleted");
}

/// The summary of what came in since the one before (under mu), written on a thread of its own unless `now` (the exit, a
/// lost device: the process ends next)
void summarize(bool now) {
    if (!current) return;
    where_now();
    read_settings();
    {   // (the renderer's events, onto the map they ended on: there are few)
        int64_t translated = 0, failed = 0;
        shader_stats(&translated, &failed);
        current->event[pr::kProgramFailed] += failed - l_failed, l_failed = failed;
        for (int e = 0; e < pr::kEvents; e++) current->event[e] += events[e].exchange(0);
    }
    bool any = false;
    for (const auto& [n, t] : maps) any = any || t.any();
    if (!any) return;   // (nothing since the last: the exit right after a lost device's summary)
    pr::Memory mem;
    uint64_t used = 0, allowed = 0;
    if (vram_usage_now(&used, &allowed)) mem.vram_used_gb = used / 1073741824.0, mem.vram_allowed_gb = allowed / 1073741824.0;
    mem.ram_used_gb = ram_used_gb();
    const double session_s = (clock_ns() - started_ns) / 1e9;
    std::string json = pr::summary_json(machine, first, session_s, current_map, &mem, maps);
    char name[48];
    snprintf(name, sizeof name, "%lld-%d", (long long)std::time(nullptr), seq++);
    for (auto& [n, t] : maps)
        if (t.any()) seen_maps[n] = true;
    maps.clear();
    current = &maps[current_map];
    current->fresh = !seen_maps.count(current_map);   // (a map entered just before this summary, with nothing yet: still new)
    first = false;
    if (now) return write_summary(folder, name, json);
    std::thread([dir = folder, n = std::string(name), j = std::move(json)] { write_summary(dir, n, j); }).detach();
}

// (an exit that runs atexit: Windows' ExitProcess, exit(). The window's close writes it first, mkwvk_close_asked; then
// this finds nothing new. Only tried: ExitProcess has already ended the other threads, and one may have held mu)
void at_exit() {
    if (!perf_measure.load(std::memory_order_relaxed)) return;
    std::unique_lock lk(mu, std::try_to_lock);
    if (!lk) return;
    summarize(true);
    states_queue();
}

/// Whether the summary should be measured now: the row is on and the build has an address (read once)
bool wanted() {
    static const bool available = !address_now().empty();
    return available && opts.send_perf == 1;
}

/// The row changed (checked about once a second, under mu): on starts a session's measuring, off deletes what was kept
void follow_row() {
    const bool on = wanted(), was = perf_measure.load(std::memory_order_relaxed);
    if (on == was) return;
    if (!on) return switch_off();
    folder = game_dir_path();
    {   // (a summary cut short by an exit before: its .tmp, never sent and never counted)
        std::error_code ec;
        for (fsys::directory_iterator it(folder / "perf_pending", ec), end; !ec && it != end; it.increment(ec))
            if (it->path().extension() == ".tmp") fsys::remove(it->path(), ec);
    }
    static bool registered = false;
    if (!registered) registered = true, std::atexit(at_exit);
    period = pr::summary_period(getenv("RX_VK_PERF_INTERVAL"));
    started_ns = clock_ns();
    int64_t translated = 0;
    shader_stats(&translated, &l_failed);   // (the renderer's counts so far: only what comes after is counted)
    l_gpu = gpu_time_ns();
    read_stamps();
    logf("vk: performance statistics on (the Send performance statistics row): a summary every %.0f s into perf_pending", period);
    perf_measure.store(true, std::memory_order_relaxed);
}

}   // namespace

bool telemetry_available() {
    static const bool available = !address_now().empty();
    return available;
}

void perf_event(int e) {
    if (e >= 0 && e < pr::kEvents && perf_measure.load(std::memory_order_relaxed)) events[e].fetch_add(1, std::memory_order_relaxed);
}

void perf_frame_shown() {
    static std::atomic<unsigned> calls{0};
    // (off: a look at the row every 64th frame, and nothing else)
    if (!perf_measure.load(std::memory_order_relaxed)) {
        if ((calls.fetch_add(1, std::memory_order_relaxed) & 63) == 0) {
            std::lock_guard lk(mu);
            follow_row();
        }
        if (!perf_measure.load(std::memory_order_relaxed)) return;
    }
    const int64_t now = clock_ns();
    // (the renderer's counters since the frame before: what the slow frames waited on)
    static int64_t l_prof[PROF_COUNT];
    static double l_wait_ms, l_shader_ms;
    double wait_ms_total = 0;
    {
        int64_t skips = 0, waits = 0;
        draw_stats3(&skips, &waits, &wait_ms_total);
    }
    const double shader_ms = draw_shader_wait_ms();
    const auto dp = [](int i) { return (prof_ns[i] - l_prof[i]) / 1e6; };
    pr::Waits w;
    w.translate = shader_ms - l_shader_ms;
    w.pipeline = std::max(0.0, (wait_ms_total - l_wait_ms) - w.translate);
    w.upload = dp(PROF_UPLOAD);
    w.memsync = dp(PROF_MEMSYNC);
    for (int i = 0; i < PROF_COUNT; i++) l_prof[i] = prof_ns[i];
    l_wait_ms = wait_ms_total, l_shader_ms = shader_ms;
    const int64_t gpu = gpu_time_ns();
    fs::Frame f;
    f.gpu_ms = (float)std::max<int64_t>(0, gpu - l_gpu) / 1e6f;
    l_gpu = gpu;

    std::lock_guard lk(mu);
    const int64_t before = last_frame_ns;
    last_frame_ns = now;
    // (the first frame only takes the counters: they hold everything since the game started; a gap over 2 s, a pause or a
    // stall of the window, is no frame)
    if (!primed || !before || now - before > 2000000000LL) {
        primed = true;
        return;
    }
    f.ms = (float)((now - before) / 1e6);
    if ((calls.fetch_add(1, std::memory_order_relaxed) & 63) == 0) follow_row();
    if (!perf_measure.load(std::memory_order_relaxed)) return;
    if (!current || since_map >= kMapEvery) {
        // (a 17th map in one period: that summary now, with the 16 it has, then the new map in the next)
        if (where_now()) summarize(false), since_summary = 0, where_now();
        since_map = 0;
    }
    // (the target: the cap's. A hitch is twice the longer of the target and the recent average, before this frame: a frame
    // twice the usual, never every frame of a game the GPU holds under its cap)
    const double usual = avg_ms ? avg_ms : f.ms;
    avg_ms = usual * 0.98 + f.ms * 0.02;
    const double target = 1000.0 / fps_cap_now();
    pr::add_frame(*current, f, w, target, last_ms, usual);
    last_ms = f.ms;
    since_map += f.ms / 1000.0, since_summary += f.ms / 1000.0;
    if (since_summary >= period) summarize(false), since_summary = 0;
}

void perf_report_final(int event) {
    const bool measuring = perf_measure.load(std::memory_order_relaxed);
    if (measuring) perf_event(event);
    std::lock_guard lk(mu);
    if (measuring) summarize(true);
    states_queue();   // (off: logs why it queues nothing)
}

}   // namespace mkwvk

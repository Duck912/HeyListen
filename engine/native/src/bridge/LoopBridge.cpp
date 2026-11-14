// HeyListen native engine: the key-0xa0 draw function's entries, in place (hl/bridge/LoopBridge.h).
#include "hl/bridge/LoopBridge.h"

#include <cstring>

namespace hl::bridge {

namespace {
// GOT slots of the game: the global switch's pointer (its u32 set: the commands are written into the command memory
// instead of called), the method the model's class has in the common case (compared with its vtable's +488), and the
// sub-mesh submission method the drawable's class has in the common case (compared with its vtable's +0x58)
constexpr GuestAddr kSwitchSlot = 0x84ca2dd8;
constexpr GuestAddr kModelMethodSlot = 0x84ca30f8;
constexpr GuestAddr kSubmitMethodSlot = 0x84cb5dc0;

// The block's fields the function reads and writes (the render-state block of gfx-record-job.md section 8)
constexpr uint64_t kBlockContext = 40, kBlockPass = 56, kBlockGeometry = 59, kBlockMode = 92, kBlockKeyApplied = 90,
                   kBlockEnableApplied = 91, kBlockTriangles = 80, kBlockFrame = 76, kBlockMask = 104,
                   kBlockQuery = 192, kBlockQueryIndex = 200;

struct Mem {
    GuestRuntime& rt;
    template <class T>
    T rd(GuestAddr a) const {
        T v;
        std::memcpy(&v, rt.ptr(a), sizeof v);
        return v;
    }
    template <class T>
    void wr(GuestAddr a, T v) const {
        std::memcpy(rt.ptr(a), &v, sizeof v);
        rt.written(a, sizeof v);
    }
    bool inline_write() const { return rd<uint32_t>(rd<uint64_t>(kSwitchSlot)) != 0; }
};
}   // namespace

const char* entry_way_name(EntryWay way) {
    static constexpr const char* names[] = {"native",     "masked",   "enable",    "bit10",     "state",     "model",
                                            "inline",     "method",    "geometry",  "flags",     "bit5",
                                            "indirect",   "instances", "norecorded", "nolist"};
    static_assert(sizeof names / sizeof names[0] == (size_t)EntryWay::Count);
    return (size_t)way < (size_t)EntryWay::Count ? names[(size_t)way] : "?";
}

EntryWay KeyLoopBridge::classify(const KeyLoopCall& call, GuestAddr entry, KeyLoopEntry& plan) const {
    const Mem m{runtime_};
    const GuestAddr block = call.block;
    const uint32_t kind = m.rd<uint32_t>(entry + 8);
    if (!(kind & (uint32_t)call.mask)) return EntryWay::Masked;
    const uint16_t h = m.rd<uint16_t>(entry + 14);
    if (((h >> 4) & 1u) != (uint32_t)(int32_t)m.rd<int8_t>(block + kBlockEnableApplied)) return EntryWay::EnableChange;
    uint32_t key = h & 0xf;
    const uint32_t block_mask = m.rd<uint32_t>(block + kBlockMask);
    if (block_mask & kind) {
        if ((h >> 10) & 1) return EntryWay::SubKeyBit10;
        key |= 0xffffff80u;
    }
    if (m.rd<uint8_t>(block + kBlockKeyApplied) != (uint8_t)key) return EntryWay::StateChange;

    plan = KeyLoopEntry{};
    plan.entry = entry;
    plan.drawable = m.rd<uint64_t>(entry + 16);
    const GuestAddr drawable = plan.drawable;
    plan.pass = m.rd<uint8_t>(block + kBlockPass);
    plan.model = m.rd<uint64_t>(drawable + 8);
    const GuestAddr model = plan.model;
    const GuestAddr passes = m.rd<uint64_t>(model + 64) + ((uint64_t)plan.pass << 8);
    plan.query = m.rd<uint64_t>(passes + 104);
    if (m.rd<uint64_t>(m.rd<uint64_t>(model) + 488) != m.rd<uint64_t>(kModelMethodSlot)) return EntryWay::ModelClass;
    plan.base = m.rd<uint32_t>(passes + 120);
    plan.index = (uint32_t)(m.rd<uint16_t>(drawable + 24) + (uint32_t)m.rd<uint16_t>(model + 244) * plan.pass);
    plan.reset = plan.query && m.rd<uint64_t>(model + 72) == 0;
    const bool conditional = (block_mask & kind) != 0;
    if (plan.reset && m.inline_write()) return EntryWay::InlineWrite;
    if (conditional) {
        const uint64_t last = m.rd<uint64_t>(block + kBlockQuery);
        const uint32_t last_index = m.rd<uint32_t>(block + kBlockQueryIndex);
        if (!(plan.query == last && plan.index == last_index)) {
            if (m.inline_write()) return EntryWay::InlineWrite;
            plan.conditional = plan.query != 0;
            plan.no_query = plan.query == 0;
            plan.was_conditional = last != 0;
        }
    }
    plan.sub_mesh = (h >> 6) & 0xf;
    if (m.rd<uint64_t>(m.rd<uint64_t>(drawable) + 88) != m.rd<uint64_t>(kSubmitMethodSlot)) return EntryWay::Method;
    plan.in_range = plan.sub_mesh < ((m.rd<uint16_t>(drawable + 30) >> 4) & 0xfu);
    if (plan.in_range) {
        plan.resource = m.rd<uint64_t>(drawable + 104);
        const GuestAddr resource = plan.resource;
        if (m.rd<uint8_t>(resource + 1694) != m.rd<uint8_t>(block + 59)) return EntryWay::Geometry;
        const uint32_t drawn = m.rd<uint32_t>(drawable + 84);
        if ((call.flags & 8u) & drawn) return EntryWay::FlagsMask;
        if ((drawn >> 5) & 1) return EntryWay::FlagsBit5;
        if (drawn & call.flags & 0x51u) return EntryWay::Indirect;
        if (m.rd<uint16_t>(drawable + 92) > 1) return EntryWay::Instances;
        plan.record = m.rd<uint64_t>(resource + 1592) + (uint64_t)plan.sub_mesh * 0x270;
        if (!(m.rd<uint8_t>(plan.record + 88) & 1)) return EntryWay::NoRecorded;
        if (!((m.rd<uint8_t>(block + kBlockMode) >> 5) & 1)) return EntryWay::NoListAllowed;
        if (m.inline_write()) return EntryWay::InlineWrite;
    }
    plan.command_buffer = m.rd<uint64_t>(m.rd<uint64_t>(block + kBlockContext) + 216);
    return EntryWay::Native;
}

namespace {
// The byte the report marks used: the query's flag array at the report's slot, wrapped to the ring's size
GuestAddr report_byte(const Mem& m, const KeyLoopEntry& p) {
    const uint32_t per = (uint32_t)m.rd<uint16_t>(p.query + 12) * m.rd<uint8_t>(p.query + 15);
    const uint32_t used = per + p.base + p.index, size = m.rd<uint32_t>(p.query + 24);
    return m.rd<uint64_t>(p.query + 32) + (int64_t)(int32_t)(used < size ? used : 0);
}
}   // namespace

void KeyLoopBridge::snapshot_prepare(GuestAddr block, GuestAddr command_buffer, uint8_t* bytes) const {
    std::memcpy(bytes, runtime_.ptr(block), 352);
    bytes[352] = command_buffer ? *(const uint8_t*)runtime_.ptr(command_buffer + 0x10) : 0;
}

void KeyLoopBridge::restore_prepare(GuestAddr block, GuestAddr command_buffer, const uint8_t* bytes) const {
    std::memcpy(runtime_.ptr(block), bytes, 352);
    runtime_.written(block, 352);
    if (command_buffer) {
        *(uint8_t*)runtime_.ptr(command_buffer + 0x10) = bytes[352];
        runtime_.written(command_buffer + 0x10, 1);
    }
}

void KeyLoopBridge::snapshot(const KeyLoopCall& call, const KeyLoopEntry& p, uint8_t* bytes) const {
    const Mem m{runtime_};
    snapshot_prepare(call.block, p.command_buffer, bytes);
    uint32_t stamp = 0;
    uint8_t used = 0;
    if (p.reset) stamp = m.rd<uint32_t>(p.query + 40), used = m.rd<uint8_t>(report_byte(m, p));
    std::memcpy(bytes + 353, &stamp, 4);
    bytes[357] = used;
}

void KeyLoopBridge::restore(const KeyLoopCall& call, const KeyLoopEntry& p, const uint8_t* bytes) const {
    const Mem m{runtime_};
    restore_prepare(call.block, p.command_buffer, bytes);
    if (!p.reset) return;
    uint32_t stamp;
    std::memcpy(&stamp, bytes + 353, 4);
    m.wr<uint32_t>(p.query + 40, stamp);
    m.wr<uint8_t>(report_byte(m, p), bytes[357]);
}

void KeyLoopBridge::run(const KeyLoopCall& call, const KeyLoopEntry& p, const Prepare& prepare, gfx::CommandSink& sink,
                        const gfx::NvnEntryPoints& nvn) const {
    using gfx::command;
    using gfx::Proc;
    const Mem m{runtime_};
    const GuestAddr block = call.block, cb = p.command_buffer;
    // (the byte the game's NVN command buffer object keeps to say commands were recorded: set before each call)
    auto mark = [&] { m.wr<uint8_t>(cb + 0x10, 1); };
    // (a query's buffer address, asked of the NVN layer: a query changes nothing; the stream carries it for the check)
    auto address = [&](GuestAddr buffer) {
        const gfx::Command c = command(Proc::BufferGetAddress, buffer);
        const uint64_t a = nvn.ask(c);
        sink.emit(c);
        return a;
    };
    auto slot = [&](uint32_t index) { return (uint64_t)index << 5; };
    const uint32_t per = p.query ? (uint32_t)m.rd<uint16_t>(p.query + 12) * m.rd<uint8_t>(p.query + 15) : 0;

    if (p.reset) {
        mark();
        sink.emit(command(Proc::CommandBufferResetCounter, cb, 1));
        m.wr<uint32_t>(p.query + 40, m.rd<uint32_t>(block + kBlockFrame));
    }
    if (p.no_query) {
        // (no query to render by: the enable goes back on, and the block remembers there was none)
        if (p.was_conditional) {
            mark();
            sink.emit(command(Proc::CommandBufferSetRenderEnable, cb, 1));
        }
        m.wr<uint64_t>(block + kBlockQuery, 0);
        m.wr<uint32_t>(block + kBlockQueryIndex, 0xffffffffu);
    }
    if (p.conditional) {
        if (p.was_conditional) {
            mark();
            sink.emit(command(Proc::CommandBufferSetRenderEnable, cb, 1));
        }
        const uint64_t a = address(p.query + 80);
        mark();
        sink.emit(command(Proc::CommandBufferSetRenderEnableConditional, cb, 1, a + slot(p.index + per)));
        m.wr<uint64_t>(block + kBlockQuery, p.query);
        m.wr<uint32_t>(block + kBlockQueryIndex, p.index);
    }
    if (p.in_range) {
        const uint64_t prepared = prepare(block, p.drawable);
        if (prepared & 1) {
            mark();
            sink.emit(command(Proc::CommandBufferCallCommands, cb, 1, p.record + 80));
            m.wr<uint32_t>(block + kBlockTriangles, m.rd<uint32_t>(block + kBlockTriangles) + m.rd<uint32_t>(p.record + 616));
        }
    }
    if (p.reset) {
        // (the report's slot is recorded as used, wrapped to the ring's size; the address is not wrapped)
        const uint32_t used = per + p.base + p.index;
        const GuestAddr byte = report_byte(m, p);
        m.wr<uint8_t>(byte, m.rd<uint8_t>(byte) | 1);
        const uint64_t a = address(p.query + 80);
        mark();
        sink.emit(command(Proc::CommandBufferReportCounter, cb, 1, a + slot(used)));
    }
}

}   // namespace hl::bridge

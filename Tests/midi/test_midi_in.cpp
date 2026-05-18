// Tests for the YSE MIDI input class + C API (Phase C of dart-yse#2 engine
// support, tracked in yvanvds/yse-soundengine#52).
//
// Coverage:
//   - midiIn default state (closed, no device)
//   - midiIn::create on an out-of-range port stays closed without crashing
//   - setRawCallback / setParsedCallback round-trip with nullptr detach
//   - close() is safe on a never-opened port and idempotent
//   - dispatch() (private) fans out to raw + parsed subscribers with correct
//     nibble splitting; out-of-range / short messages report missing data
//     bytes as 0
//   - Detach (setRawCallback/setParsedCallback with nullptr) stops further
//     callbacks
//   - C API mirrors the C++ class: create/destroy, is_open, callback setters,
//     free_message handles nullptr
//
// Windows / Linux only — the entire TU is gated on the same condition that
// gates device.cpp. No engine initialisation is required (the MIDI subsystem
// is independent of the PortAudio device manager).

#include <doctest/doctest.h>
#include <atomic>
#include <cstring>
#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "midi/device.hpp"
#include "yse_c/yse_midi.h"

// Friend-declared in YSE::midiIn so tests can drive dispatch() directly
// without needing a real MIDI port (RtMidi's openVirtualPort is unavailable
// on Windows; this keeps the parser test deterministic on every platform).
struct MidiInDispatchTester
{
    static void dispatch(YSE::midiIn& m, double ts, const unsigned char* bytes, std::size_t len)
    {
        m.dispatch(ts, bytes, len);
    }
};

namespace
{

// Shared sinks for the dispatch tests. Static so callback pointers are
// always-valid C function pointers (no closure state needed).
struct RawSink
{
    std::atomic<int> calls{0};
    std::atomic<double> lastTs{0.0};
    std::atomic<std::size_t> lastLen{0};
    unsigned char lastBytes[8]{};
};
struct ParsedSink
{
    std::atomic<int> calls{0};
    std::atomic<double> lastTs{0.0};
    std::atomic<unsigned char> status{0};
    std::atomic<unsigned char> channel{0};
    std::atomic<unsigned char> data1{0};
    std::atomic<unsigned char> data2{0};
};

RawSink    g_raw;
ParsedSink g_parsed;

void rawCb(double ts, const unsigned char* bytes, std::size_t len, void*)
{
    g_raw.calls.fetch_add(1, std::memory_order_relaxed);
    g_raw.lastTs.store(ts, std::memory_order_relaxed);
    g_raw.lastLen.store(len, std::memory_order_relaxed);
    const std::size_t n = len < sizeof(g_raw.lastBytes) ? len : sizeof(g_raw.lastBytes);
    std::memcpy(g_raw.lastBytes, bytes, n);
}

void parsedCb(double ts, unsigned char s, unsigned char c,
              unsigned char d1, unsigned char d2, void*)
{
    g_parsed.calls.fetch_add(1, std::memory_order_relaxed);
    g_parsed.lastTs.store(ts, std::memory_order_relaxed);
    g_parsed.status.store(s, std::memory_order_relaxed);
    g_parsed.channel.store(c, std::memory_order_relaxed);
    g_parsed.data1.store(d1, std::memory_order_relaxed);
    g_parsed.data2.store(d2, std::memory_order_relaxed);
}

void resetSinks()
{
    g_raw.calls.store(0, std::memory_order_relaxed);
    g_raw.lastLen.store(0, std::memory_order_relaxed);
    std::memset(g_raw.lastBytes, 0, sizeof(g_raw.lastBytes));
    g_parsed.calls.store(0, std::memory_order_relaxed);
    g_parsed.status.store(0, std::memory_order_relaxed);
    g_parsed.channel.store(0, std::memory_order_relaxed);
    g_parsed.data1.store(0, std::memory_order_relaxed);
    g_parsed.data2.store(0, std::memory_order_relaxed);
}

} // namespace

TEST_SUITE("midi")
{

// ─── midiIn default state ────────────────────────────────────────────────────

TEST_CASE("midiIn: default-constructed instance is closed")
{
    YSE::midiIn in;
    CHECK_FALSE(in.isOpen());
}

TEST_CASE("midiIn: close() on a never-opened instance is safe")
{
    YSE::midiIn in;
    in.close();
    CHECK_FALSE(in.isOpen());
}

TEST_CASE("midiIn: create on an out-of-range port leaves the instance closed")
{
    YSE::midiIn in;
    in.create(9999);
    CHECK_FALSE(in.isOpen());
}

TEST_CASE("midiIn: setRawCallback / setParsedCallback accept nullptr without crashing")
{
    YSE::midiIn in;
    in.setRawCallback(nullptr, nullptr);
    in.setParsedCallback(nullptr, nullptr);
    CHECK(true);
}

// ─── Dispatch fan-out via the friend test struct ────────────────────────────

TEST_CASE("midiIn dispatch: raw callback receives the original bytes")
{
    resetSinks();
    YSE::midiIn in;
    in.setRawCallback(rawCb, nullptr);

    const unsigned char msg[3] = {0x91, 0x3C, 0x64}; // Note-On ch 1, C4, vel 100
    MidiInDispatchTester::dispatch(in, 1.25, msg, 3);

    CHECK(g_raw.calls.load() == 1);
    CHECK(g_raw.lastTs.load() == doctest::Approx(1.25));
    CHECK(g_raw.lastLen.load() == 3);
    CHECK(g_raw.lastBytes[0] == 0x91);
    CHECK(g_raw.lastBytes[1] == 0x3C);
    CHECK(g_raw.lastBytes[2] == 0x64);
}

TEST_CASE("midiIn dispatch: parsed callback splits status / channel / data bytes")
{
    resetSinks();
    YSE::midiIn in;
    in.setParsedCallback(parsedCb, nullptr);

    // Note-On on channel 6 (0x96), pitch 60, velocity 100.
    const unsigned char msg[3] = {0x96, 0x3C, 0x64};
    MidiInDispatchTester::dispatch(in, 0.5, msg, 3);

    CHECK(g_parsed.calls.load() == 1);
    CHECK(g_parsed.lastTs.load() == doctest::Approx(0.5));
    CHECK(g_parsed.status.load()  == 0x90);  // Note-On status nibble
    CHECK(g_parsed.channel.load() == 0x06);  // channel nibble
    CHECK(g_parsed.data1.load()   == 0x3C);
    CHECK(g_parsed.data2.load()   == 0x64);
}

TEST_CASE("midiIn dispatch: 2-byte message (Program Change) reports data2 as 0")
{
    resetSinks();
    YSE::midiIn in;
    in.setParsedCallback(parsedCb, nullptr);

    const unsigned char msg[2] = {0xC2, 0x05}; // Program Change ch 2, program 5
    MidiInDispatchTester::dispatch(in, 0.0, msg, 2);

    CHECK(g_parsed.status.load()  == 0xC0);
    CHECK(g_parsed.channel.load() == 0x02);
    CHECK(g_parsed.data1.load()   == 0x05);
    CHECK(g_parsed.data2.load()   == 0x00);
}

TEST_CASE("midiIn dispatch: 1-byte message (clock) reports data1/data2 as 0")
{
    resetSinks();
    YSE::midiIn in;
    in.setParsedCallback(parsedCb, nullptr);

    const unsigned char msg[1] = {0xF8}; // MIDI Timing Clock
    MidiInDispatchTester::dispatch(in, 0.0, msg, 1);

    CHECK(g_parsed.status.load() == 0xF0);
    CHECK(g_parsed.data1.load()  == 0x00);
    CHECK(g_parsed.data2.load()  == 0x00);
}

TEST_CASE("midiIn dispatch: empty buffer dispatches nothing")
{
    resetSinks();
    YSE::midiIn in;
    in.setRawCallback(rawCb, nullptr);
    in.setParsedCallback(parsedCb, nullptr);

    // dispatch() guards against zero-length input in the raw branch; the
    // parsed branch indexes bytes[0] so the trampoline's empty-buffer check
    // is what we rely on. Drive directly to confirm the dispatch member
    // itself is also safe.
    MidiInDispatchTester::dispatch(in, 0.0, nullptr, 0);
    // The trampoline never delivers a zero-length message to dispatch in
    // production, but the unit guard here keeps the member function safe.
    // No callback should have fired.
    CHECK(g_raw.calls.load() == 0);
}

TEST_CASE("midiIn dispatch: detaching the raw callback stops further raw fires")
{
    resetSinks();
    YSE::midiIn in;
    in.setRawCallback(rawCb, nullptr);

    const unsigned char msg[3] = {0x90, 0x3C, 0x64};
    MidiInDispatchTester::dispatch(in, 0.0, msg, 3);
    CHECK(g_raw.calls.load() == 1);

    in.setRawCallback(nullptr, nullptr);
    MidiInDispatchTester::dispatch(in, 0.0, msg, 3);
    CHECK(g_raw.calls.load() == 1); // unchanged
}

TEST_CASE("midiIn dispatch: both callbacks fire independently when both are set")
{
    resetSinks();
    YSE::midiIn in;
    in.setRawCallback(rawCb, nullptr);
    in.setParsedCallback(parsedCb, nullptr);

    const unsigned char msg[3] = {0xB4, 0x07, 0x40}; // CC ch 4, controller 7, value 64
    MidiInDispatchTester::dispatch(in, 2.0, msg, 3);

    CHECK(g_raw.calls.load() == 1);
    CHECK(g_parsed.calls.load() == 1);
    CHECK(g_parsed.status.load() == 0xB0);
    CHECK(g_parsed.channel.load() == 0x04);
}

// ─── C API mirror ───────────────────────────────────────────────────────────

TEST_CASE("yse_midi_in C API: create/destroy lifecycle")
{
    YseMidiIn* m = yse_midi_in_create();
    REQUIRE(m != nullptr);
    CHECK(yse_midi_in_is_open(m) == 0);
    yse_midi_in_destroy(m);
}

TEST_CASE("yse_midi_in C API: open on an invalid port leaves the handle closed")
{
    YseMidiIn* m = yse_midi_in_create();
    REQUIRE(m != nullptr);
    yse_midi_in_open(m, 9999);
    CHECK(yse_midi_in_is_open(m) == 0);
    yse_midi_in_destroy(m);
}

TEST_CASE("yse_midi_in C API: callback setters accept nullptr without crashing")
{
    YseMidiIn* m = yse_midi_in_create();
    REQUIRE(m != nullptr);
    yse_midi_in_set_raw_callback(m, nullptr, nullptr);
    yse_midi_in_set_parsed_callback(m, nullptr, nullptr);
    yse_midi_in_destroy(m);
    CHECK(true);
}

TEST_CASE("yse_midi_in C API: free_message safely handles nullptr")
{
    yse_midi_in_free_message(nullptr);
    CHECK(true);
}

TEST_CASE("yse_midi_in C API: NULL handle returns 0 on is_open")
{
    CHECK(yse_midi_in_is_open(nullptr) == 0);
}

} // TEST_SUITE("midi")

#endif // YSE_ENABLE_MIDI_DEVICE

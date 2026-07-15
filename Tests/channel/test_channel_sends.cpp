// Tests for the channel send/return buses (issue #165, part of epic #146).
//
// Design contract: docs/design/send_return_buses.md. The feature adds aux
// send/return routing on top of the channel tree: any channel can send a
// ramped, scaled copy of its output into a return bus; the return applies its
// own insert chain (e.g. a plate reverb) and folds into MainMix after the
// source tree, before the master fader. Return→return sends are allowed as an
// acyclic DAG; cycles are rejected at wiring time on the control thread.
//
// Three layers of coverage:
//   1. Deterministic impl-level tests of the send accumulation math — the
//      ramped multiply-accumulate is click-free and sample-accurate. Driven
//      directly on CHANNEL::implementationObject (the pattern test_channel_dsp
//      uses), engineInit()-guarded because setup() sizes buffers from the
//      device layout.
//   2. Control-thread wiring-graph tests — generation indexing and cycle
//      rejection, driven through the manager's public graph API. These need no
//      audio device, so they run on headless CI.
//   3. Interface-level wiring tests — makeReturn / send / getSendLevel and the
//      wiring-validation rejections through the public YSE::channel surface.
//
// The concurrent render / TSan gate lives in the separate "sendstress" suite at
// the bottom (offline engine, its own ctest process — see Tests/CMakeLists.txt).

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "channel/channelInterface.hpp"
#include "channel/channelImplementation.h"
#include "channel/channelManager.h"
#include "channel/channelMessage.h"
#include "dsp/dspObject.hpp"
#include "dsp/modules/plateReverb.hpp"
#include "sound/soundInterface.hpp"
#include "sound/soundManager.h"
#include "implementations/logImplementation.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {

  using YSE::CHANNEL::implementationObject;

  // ─── impl-level helpers (mirror test_channel_dsp.cpp) ──────────────────────

  // Size an impl's `out` (and `sends`) to the device layout, then mark it READY.
  // Requires a live engine (setup() reads CHANNEL::Manager().getNumberOfOutputs()).
  // The READY promotion stands in for the audio thread's readyCheck (SETUP ->
  // READY) so a send taps into it: accumulateSend gates on the target reaching
  // OBJECT_READY (issue #165), which a directly-driven impl would otherwise never
  // reach.
  void primeImpl(implementationObject& impl) {
    impl.setStatus(YSE::OBJECT_CREATED);
    impl.setup();
    impl.setStatus(YSE::OBJECT_READY);
  }

  void fill(std::vector<YSE::DSP::buffer>& bufs, float value) {
    for (auto& b : bufs) {
      float* p = b.getPtr();
      const UInt n = b.getLength();
      for (UInt i = 0; i < n; ++i)
        p[i] = value;
    }
  }

  bool allEqual(std::vector<YSE::DSP::buffer>& bufs, float value) {
    for (auto& b : bufs) {
      float* p = b.getPtr();
      const UInt n = b.getLength();
      for (UInt i = 0; i < n; ++i)
        if (p[i] != doctest::Approx(value)) return false;
    }
    return true;
  }

  // Wire src's slot at `target` via the ADD_SEND message the interface posts.
  void wireSend(implementationObject& src, implementationObject* target, int slot, bool preFader) {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::ADD_SEND;
    m.send.target = target;
    m.send.slot = slot;
    m.send.preFader = preFader;
    src.parseMessage(m);
  }

  void setSendLevelMsg(implementationObject& src, int slot, float level) {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::SEND_LEVEL;
    m.sendLevel.slot = slot;
    m.sendLevel.level = level;
    src.parseMessage(m);
  }

  void removeSendMsg(implementationObject& src, int slot) {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::REMOVE_SEND;
    m.uintValue = (UInt)slot;
    src.parseMessage(m);
  }

  // Run one post-fader tap and let the ramp settle so a subsequent tap runs at a
  // constant level (lastLevel == newLevel).
  void settlePostFader(implementationObject& src, implementationObject& target) {
    src.runSendTaps(false);
    target.clearBuffers();
    src.runSendTaps(false);
  }

  void drainChannels(int iterations = 12) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      YSE::CHANNEL::Manager().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

} // namespace

TEST_SUITE("channel") {

  // ─── Message ids ───────────────────────────────────────────────────────────

  TEST_CASE("channel sends: the new send message ids are all distinct") {
    using namespace YSE::CHANNEL;
    CHECK(ADD_SEND != SEND_LEVEL);
    CHECK(ADD_SEND != REMOVE_SEND);
    CHECK(ADD_SEND != SET_GENERATION);
    CHECK(SEND_LEVEL != REMOVE_SEND);
    CHECK(SEND_LEVEL != SET_GENERATION);
    CHECK(REMOVE_SEND != SET_GENERATION);
    // and distinct from the pre-existing ids
    CHECK(ADD_SEND != VOLUME);
    CHECK(ADD_SEND != ATTACH_DSP);
    CHECK(SET_GENERATION != ATTACH_REVERB);
  }

  // ─── Send accumulation math (acceptance: click-free, sample-accurate) ───────

  TEST_CASE("channel sends: post-fader send accumulates a scaled copy at a steady level") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), ret(nullptr);
    primeImpl(src);
    primeImpl(ret);
    REQUIRE(src.GetBuffers().size() > 0);

    wireSend(src, &ret, 0, false);
    setSendLevelMsg(src, 0, 0.5f);

    fill(src.GetBuffers(), 1.0f);
    ret.clearBuffers();
    settlePostFader(src, ret); // ramp 0->0.5 then run at steady 0.5
    // src is 1.0 everywhere, level 0.5 -> return holds 0.5.
    CHECK(allEqual(ret.GetBuffers(), 0.5f));
  }

  TEST_CASE("channel sends: a fresh send ramps up from silence, sample-accurately (click-free)") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), ret(nullptr);
    primeImpl(src);
    primeImpl(ret);
    REQUIRE(src.GetBuffers().size() > 0);

    wireSend(src, &ret, 0, false);
    setSendLevelMsg(src, 0, 0.5f); // lastLevel = 0, newLevel = 0.5

    fill(src.GetBuffers(), 1.0f);
    ret.clearBuffers();
    src.runSendTaps(false); // first block: ramp 0 -> 0.5

    // The ramp is fused into the MAC exactly like adjustVolume(): sample j gets
    // src[j] * (lastLevel + step*j), step = (newLevel-lastLevel)/BUFSIZE.
    const float step = 0.5f / static_cast<float>(YSE::STANDARD_BUFFERSIZE);
    float* p = ret.GetBuffers()[0].getPtr();
    CHECK(p[0] == doctest::Approx(0.f)); // starts at silence — no click
    CHECK(p[1] == doctest::Approx(step));
    CHECK(p[64] == doctest::Approx(step * 64.f));
    CHECK(p[YSE::STANDARD_BUFFERSIZE - 1] ==
          doctest::Approx(step * static_cast<float>(YSE::STANDARD_BUFFERSIZE - 1)));
  }

  TEST_CASE(
      "channel sends: a level change ramps linearly across the block, never steps (no zipper)") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), ret(nullptr);
    primeImpl(src);
    primeImpl(ret);
    REQUIRE(src.GetBuffers().size() > 0);

    wireSend(src, &ret, 0, false);
    setSendLevelMsg(src, 0, 0.2f);
    fill(src.GetBuffers(), 1.0f);
    ret.clearBuffers();
    settlePostFader(src, ret); // settle at 0.2 (lastLevel becomes 0.2)

    // Now move the target to 0.8 and tap once: the whole block is a single
    // linear ramp 0.2 -> 0.8, so consecutive samples differ by a constant step
    // (a zipper/step would show a discontinuity somewhere).
    setSendLevelMsg(src, 0, 0.8f);
    fill(src.GetBuffers(), 1.0f);
    ret.clearBuffers();
    src.runSendTaps(false);

    const float step = (0.8f - 0.2f) / static_cast<float>(YSE::STANDARD_BUFFERSIZE);
    float* p = ret.GetBuffers()[0].getPtr();
    CHECK(p[0] == doctest::Approx(0.2f));
    for (UInt j = 1; j < YSE::STANDARD_BUFFERSIZE; ++j) {
      CHECK((p[j] - p[j - 1]) == doctest::Approx(step)); // constant slope: no zipper
    }
  }

  TEST_CASE("channel sends: continuous setSendLevel writes stay click-free block to block") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), ret(nullptr);
    primeImpl(src);
    primeImpl(ret);
    REQUIRE(src.GetBuffers().size() > 0);
    wireSend(src, &ret, 0, false);

    // Drive a new target level every block (the modulation-target usage pattern)
    // and assert every block's first sample continues smoothly from the previous
    // block's last sample — i.e. the multiplier never jumps.
    fill(src.GetBuffers(), 1.0f);
    float prevLast = 0.f;
    float target = 0.f;
    for (int block = 0; block < 32; ++block) {
      target = 0.5f + 0.4f * std::sin(0.3f * static_cast<float>(block));
      setSendLevelMsg(src, 0, target);
      ret.clearBuffers();
      src.runSendTaps(false);
      float* p = ret.GetBuffers()[0].getPtr();
      // src is a constant 1.0, so p[] is exactly the gain envelope. The first
      // sample equals the previous block's ending multiplier (continuity), and
      // within a block the slope is bounded by one block's worth of change.
      CHECK(p[0] == doctest::Approx(prevLast));
      prevLast = p[YSE::STANDARD_BUFFERSIZE - 1] +
                 (target - prevLast) / static_cast<float>(YSE::STANDARD_BUFFERSIZE);
    }
  }

  TEST_CASE("channel sends: pre-fader and post-fader slots tap only on their own phase") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), retPre(nullptr), retPost(nullptr);
    primeImpl(src);
    primeImpl(retPre);
    primeImpl(retPost);
    REQUIRE(src.GetBuffers().size() > 0);

    wireSend(src, &retPre, 0, /*preFader*/ true);
    setSendLevelMsg(src, 0, 1.0f);
    wireSend(src, &retPost, 1, /*preFader*/ false);
    setSendLevelMsg(src, 1, 1.0f);

    // Pre-fader phase: only the pre slot taps.
    fill(src.GetBuffers(), 1.0f);
    retPre.clearBuffers();
    retPost.clearBuffers();
    src.runSendTaps(true);
    CHECK_FALSE(allEqual(retPre.GetBuffers(), 0.f)); // pre got signal (ramped)
    CHECK(allEqual(retPost.GetBuffers(), 0.f)); // post untouched

    // Post-fader phase: only the post slot taps.
    retPre.clearBuffers();
    retPost.clearBuffers();
    src.runSendTaps(false);
    CHECK(allEqual(retPre.GetBuffers(), 0.f)); // pre untouched this phase
    CHECK_FALSE(allEqual(retPost.GetBuffers(), 0.f)); // post got signal
  }

  TEST_CASE("channel sends: REMOVE_SEND stops a slot from accumulating") {
    if (!TestHelpers::engineInit()) return;
    implementationObject src(nullptr), ret(nullptr);
    primeImpl(src);
    primeImpl(ret);
    REQUIRE(src.GetBuffers().size() > 0);

    wireSend(src, &ret, 0, false);
    setSendLevelMsg(src, 0, 0.5f);
    fill(src.GetBuffers(), 1.0f);
    ret.clearBuffers();
    settlePostFader(src, ret);
    CHECK_FALSE(allEqual(ret.GetBuffers(), 0.f)); // active: it accumulates

    removeSendMsg(src, 0);
    ret.clearBuffers();
    src.runSendTaps(false);
    CHECK(allEqual(ret.GetBuffers(), 0.f)); // detached: nothing added
  }

  // ─── Control-thread wiring graph: generations + cycle rejection ─────────────
  // (No audio device needed — the graph is pure control-thread bookkeeping.)

  TEST_CASE("channel sends: the return graph layers generations and rejects cycles") {
    auto& M = YSE::CHANNEL::Manager();
    // Three bare impls used only as graph nodes (identity by pointer).
    implementationObject a(nullptr), b(nullptr), c(nullptr);
    M.registerReturnGraph(&a);
    M.registerReturnGraph(&b);
    M.registerReturnGraph(&c);

    // Fresh returns are generation 0.
    CHECK(M.returnGenerationOf(&a) == 0);
    CHECK(M.returnGenerationOf(&b) == 0);

    // a -> b : b is now fed by a generation-0 return, so it is generation 1.
    CHECK(M.tryAddReturnEdge(&a, &b) == true);
    CHECK(M.returnGenerationOf(&a) == 0);
    CHECK(M.returnGenerationOf(&b) == 1);

    // b -> c : the chain a -> b -> c makes c generation 2.
    CHECK(M.tryAddReturnEdge(&b, &c) == true);
    CHECK(M.returnGenerationOf(&c) == 2);

    // c -> a would close the cycle a->b->c->a: rejected, graph unchanged.
    CHECK(M.tryAddReturnEdge(&c, &a) == false);
    CHECK(M.returnGenerationOf(&a) == 0);
    CHECK(M.returnGenerationOf(&c) == 2);

    // Removing a -> b drops b (and the chain) back to generation 0.
    M.removeReturnEdge(&a, &b);
    CHECK(M.returnGenerationOf(&b) == 0);
    CHECK(M.returnGenerationOf(&c) == 1); // still fed by b

    // Cleanup: never leave dangling stack-impl pointers in the shared graph.
    M.unregisterReturnGraph(&a);
    M.unregisterReturnGraph(&b);
    M.unregisterReturnGraph(&c);
    CHECK(M.returnGenerationOf(&a) == -1);
  }

  TEST_CASE("channel sends: a self-send edge is treated as a cycle by the graph") {
    auto& M = YSE::CHANNEL::Manager();
    implementationObject a(nullptr);
    M.registerReturnGraph(&a);
    CHECK(M.tryAddReturnEdge(&a, &a) == false); // reflexive edge = cycle
    M.unregisterReturnGraph(&a);
  }

  // ─── Interface-level wiring + validation ────────────────────────────────────

  TEST_CASE("channel sends: makeReturn flags a return, create() does not") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel normal;
    normal.create("send_normal", YSE::ChannelMaster());
    YSE::channel ret;
    ret.makeReturn("send_ret");
    drainChannels();
    CHECK_FALSE(normal.isReturn());
    CHECK(ret.isReturn());
    drainChannels();
  }

  TEST_CASE("channel sends: send to a non-return target is rejected") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel src, notReturn;
    src.create("send_src_a", YSE::ChannelMaster());
    notReturn.create("send_notret", YSE::ChannelMaster());
    drainChannels();
    src.send(0, notReturn, 0.5f);
    CHECK(src.getSendLevel(0) == doctest::Approx(0.f)); // wiring not accepted
    drainChannels();
  }

  TEST_CASE("channel sends: a self-send is rejected") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel ret;
    ret.makeReturn("send_selfret");
    drainChannels();
    ret.send(0, ret, 0.5f);
    CHECK(ret.getSendLevel(0) == doctest::Approx(0.f));
    drainChannels();
  }

  TEST_CASE("channel sends: an out-of-range slot index is rejected") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel src, ret;
    src.create("send_src_b", YSE::ChannelMaster());
    ret.makeReturn("send_ret_b");
    drainChannels();
    src.send(99, ret, 0.5f); // only slots [0,4) exist by default
    CHECK(src.getSendLevel(99) == doctest::Approx(0.f));
    CHECK(src.getSendLevel(0) == doctest::Approx(0.f));
    drainChannels();
  }

  TEST_CASE("channel sends: an accepted send reports its level; clearSend detaches it") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel src, ret;
    src.create("send_src_c", YSE::ChannelMaster());
    ret.makeReturn("send_ret_c");
    drainChannels();

    src.send(0, ret, 0.4f);
    CHECK(src.getSendLevel(0) == doctest::Approx(0.4f));
    src.setSendLevel(0, 0.7f);
    CHECK(src.getSendLevel(0) == doctest::Approx(0.7f));
    src.clearSend(0);
    CHECK(src.getSendLevel(0) == doctest::Approx(0.f));
    drainChannels();
  }

  TEST_CASE("channel sends: return->return is allowed but a cycle is rejected at wiring time") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel delayRet, reverbRet;
    delayRet.makeReturn("send_delay");
    reverbRet.makeReturn("send_reverb");
    drainChannels();

    // delay -> reverb: a legal DAG edge (the delay tail washes into the reverb).
    delayRet.send(0, reverbRet, 0.5f);
    CHECK(delayRet.getSendLevel(0) == doctest::Approx(0.5f));

    // reverb -> delay would close a cycle: rejected, left unset.
    reverbRet.send(0, delayRet, 0.5f);
    CHECK(reverbRet.getSendLevel(0) == doctest::Approx(0.f));
    drainChannels();
  }

} // TEST_SUITE("channel")

// ════════════════════════════════════════════════════════════════════════════
//  sendstress — offline-render + concurrency gate (issue #165)
//
//  Runs under an offline engine (no audio device), so it executes on headless
//  CI and under the TSan/ASan sanitizer legs. Isolated in its own ctest process
//  (Tests/CMakeLists.txt) and excluded from the combined yse_unit_tests run,
//  because initOffline() drives process-global engine state.
//
//  doctest's assertion macros are not thread-safe, so the worker threads call
//  none of them (a race/UAF is caught by the sanitizer aborting; logical
//  post-conditions are asserted on the main thread after the workers join). The
//  logger writes a shared stream without a lock, so it is silenced for the
//  duration.
// ════════════════════════════════════════════════════════════════════════════

namespace {

  // A steady tone source: every source channel produces a real, band-limited AC
  // signal into its `out`, so the send taps have something to accumulate AND the
  // plate reverb on the return produces a genuine wet tail. (A pure-DC source
  // must NOT be used here: a plate reverb's comb/allpass network rejects DC, so
  // the return's wet output would read ~0 and the "signal reaches the return"
  // assertions would be measuring reverb buildup noise rather than the send
  // path.) File-scope so it outlives every sound built from it (the
  // create(dspSourceObject) contract).
  struct ToneSource : YSE::DSP::dspSourceObject {
    float ph = 0.f;
    void process(YSE::SOUND_STATUS& intent) override {
      for (UInt c = 0; c < samples.size(); ++c) {
        float* p = samples[c].getPtr();
        const UInt len = samples[c].getLength();
        float local = ph;
        for (UInt i = 0; i < len; ++i) {
          p[i] = 0.25f * std::sin(local);
          local += 0.12f;
        }
      }
      ph += 0.12f * (float)samples[0].getLength();
      intent = YSE::SS_PLAYING;
    }
    void frequency(float) override {}
  };

  ToneSource g_tone[8];
  YSE::DSP::MODULES::plateReverb g_plateA;
  YSE::DSP::MODULES::plateReverb g_plateB;

  struct LogSilencer {
    YSE::ERROR_LEVEL previous;
    LogSilencer() : previous(YSE::INTERNAL::LogImpl().getLevel()) {
      YSE::INTERNAL::LogImpl().setLevel(YSE::EL_NONE);
    }
    ~LogSilencer() {
      YSE::INTERNAL::LogImpl().setLevel(previous);
    }
  };

  // Init the offline engine once for the whole sendstress process.
  bool ensureOffline() {
    static bool done = false;
    static bool ok = false;
    if (!done) {
      YSE::System().close(); // normalize regardless of starting state
      ok = YSE::System().initOffline();
      done = true;
    }
    return ok;
  }

  // Drive the engine to quiescence: System().update() flags the control-plane
  // work (so the audio callback body actually runs the manager updates), then
  // renderOffline() runs blocks; a short sleep lets the single-threaded slow
  // pool execute the queued setup() jobs. This is the offline analogue of the
  // channel suite's drainChannels(). Needed so freshly created channels /
  // returns / sounds reach OBJECT_READY (and get linked into the returns list)
  // before the render is expected to carry signal.
  void pump(int iterations = 20) {
    for (int i = 0; i < iterations; ++i) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  bool anyNonFinite(YSE::channel& ch) {
    const int n = ch.getNumOutputs();
    for (int i = 0; i < n; ++i) {
      const float v = ch.getPeakLinearPost(i);
      if (!std::isfinite(v)) return true;
    }
    return false;
  }

} // namespace

TEST_SUITE("sendstress") {

  TEST_CASE(
      "sends stress: 8 senders into 2 reverb returns render race-free under live level moves") {
    if (!ensureOffline()) return;
    LogSilencer silence;

    YSE::channel retA, retB;
    retA.makeReturn("stress_retA");
    retB.makeReturn("stress_retB");
    retA.setDSP(&g_plateA);
    retB.setDSP(&g_plateB);

    YSE::channel src[8];
    YSE::sound snd[8];
    for (int i = 0; i < 8; ++i)
      src[i].create(("stress_src_" + std::to_string(i)).c_str(), YSE::ChannelMaster());

    // Drive lifecycle to READY, then attach a steady tone to each channel and
    // start it playing (create() does not auto-play).
    pump();
    for (int i = 0; i < 8; ++i)
      snd[i].create(g_tone[i], &src[i], 1.0f);
    pump();
    for (int i = 0; i < 8; ++i)
      snd[i].play();
    pump();

    // Every source fans out to both returns at distinct levels (the canonical
    // aux topology: 8 channels sharing 2 reverb returns).
    for (int i = 0; i < 8; ++i) {
      src[i].send(0, retA, 0.3f);
      src[i].send(1, retB, 0.2f);
    }
    pump(); // apply the wiring + warm the reverb tanks

    // Sanity before the race: the send path delivers to both returns and to the
    // master mix (dry sources + wet returns).
    CHECK(src[0].getPeakLinearPost() > 0.f);
    CHECK(YSE::ChannelMaster().getPeakLinearPost() > 0.f);
    CHECK(retA.getPeakLinearPost() > 0.f);
    CHECK(retB.getPeakLinearPost() > 0.f);

    // ── Concurrent phase (the TSan/ASan gate) ──
    // One render thread IS the audio thread: it renders blocks in a tight loop,
    // draining the per-channel message inboxes in sync() and running the send
    // accumulation + returns phase. One control thread hammers setSendLevel() on
    // every slot every iteration — the modulation-target usage pattern the design
    // mandates (§11) — and flags the manager update the render thread consumes.
    // Send levels are the shared state written by control and read (via the ramp)
    // by audio; the design's claim is that this hand-off is race-free because it
    // is a bounded lfQueue message applied in sync(), never a direct write to
    // render state. No doctest macro runs off the main thread (they are not
    // thread-safe); a race/UAF is caught by the sanitizer aborting the process,
    // and the logical post-conditions are asserted on the main thread after join.
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> blocks{0};

    std::thread render([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        YSE::System().renderOffline(1);
        blocks.fetch_add(1, std::memory_order_relaxed);
      }
    });

    std::thread control([&] {
      for (int t = 0; t < 6000; ++t) {
        for (int i = 0; i < 8; ++i) {
          const float a = 0.3f + 0.2f * std::sin(0.013f * static_cast<float>(t) + i);
          const float b = 0.2f + 0.15f * std::sin(0.017f * static_cast<float>(t) + i);
          src[i].setSendLevel(0, a);
          src[i].setSendLevel(1, b);
        }
        YSE::System().update(); // flag the update the render thread applies
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    });

    control.join();
    stop.store(true, std::memory_order_relaxed);
    render.join();

    // The audio thread kept rendering throughout the storm.
    CHECK(blocks.load() > 0);

    // Post-conditions on the main thread: both returns still carry signal and the
    // accumulation stayed bounded (no NaN/Inf reached the render path).
    pump(4);
    CHECK(retA.getPeakLinearPost() > 0.f);
    CHECK(retB.getPeakLinearPost() > 0.f);
    CHECK_FALSE(anyNonFinite(retA));
    CHECK_FALSE(anyNonFinite(retB));

    // Detach the module inserts before the returns fall out of scope.
    retA.setDSP(nullptr);
    retB.setDSP(nullptr);
    pump(4);
  }

  TEST_CASE("sends stress: a return->return (delay->reverb) chain carries signal end to end") {
    if (!ensureOffline()) return;
    LogSilencer silence;

    // reverbRet is fed only by delayRet (generation 0 -> 1). A source drives
    // delayRet; if the generation ordering is correct, the reverb return sees
    // signal (delayRet accumulates into reverbRet before reverbRet is folded).
    YSE::channel delayRet, reverbRet;
    delayRet.makeReturn("chain_delay");
    reverbRet.makeReturn("chain_reverb");
    reverbRet.setDSP(&g_plateA); // reuse a plate as the reverb payload

    YSE::channel src;
    YSE::sound snd;
    src.create("chain_src", YSE::ChannelMaster());
    pump();
    snd.create(g_tone[0], &src, 1.0f);
    pump();
    snd.play();
    pump();

    src.send(0, delayRet, 0.8f); // source -> delay return (gen 0)
    delayRet.send(0, reverbRet, 0.8f); // delay -> reverb return (gen 1)
    CHECK(delayRet.getSendLevel(0) == doctest::Approx(0.8f));

    pump(); // apply wiring
    for (int i = 0; i < 8; ++i)
      YSE::System().renderOffline(16); // let the plate tail build
    CHECK(reverbRet.getPeakLinearPost() > 0.f); // the chain delivered end to end
    CHECK_FALSE(anyNonFinite(reverbRet));

    reverbRet.setDSP(nullptr);
    pump(4);
  }

  TEST_CASE("sends stress: destroying a return while senders are live is UAF-safe") {
    if (!ensureOffline()) return;
    LogSilencer silence;

    YSE::channel src[4];
    YSE::sound snd[4];
    for (int i = 0; i < 4; ++i)
      src[i].create(("td_src_" + std::to_string(i)).c_str(), YSE::ChannelMaster());
    pump();
    for (int i = 0; i < 4; ++i)
      snd[i].create(g_tone[i], &src[i], 1.0f);
    pump();
    for (int i = 0; i < 4; ++i)
      snd[i].play();
    pump();

    {
      YSE::channel ret;
      ret.makeReturn("td_ret");
      for (int i = 0; i < 4; ++i)
        src[i].send(0, ret, 0.5f);
      pump(); // sends live into the return
    } // ~ret: OBJECT_RELEASE -> detachSends() severs every sender's slot on the
      // audio thread before the impl is freed. The senders still hold slot 0.

    // Keep rendering: the severed slots must not dereference the freed return.
    // ASan/TSan catch a use-after-free here; a plain build must not crash.
    pump(32);
    CHECK(YSE::ChannelMaster().getPeakLinearPost() >= 0.f);
  }

} // TEST_SUITE("sendstress")

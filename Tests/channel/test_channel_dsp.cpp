// Tests for the channel insert DSP chain (issue #159, part of epic #146).
//
// Covers:
//   - CHANNEL::ATTACH_DSP message id is distinct from the other channel IDs.
//   - The pre-fader insert chain audibly processes the summed channel signal
//     (a gain-inverting / gain-scaling test module applied to `out`).
//   - Chained modules (dspObject::link) both run, in order.
//   - A bypassed module passes the signal through unchanged.
//   - Attach/replace/detach follow the sound-path `calledfrom` swap discipline
//     (#298): replacing clears the old plugin's back-reference, detaching with
//     nullptr severs it, and a plugin destroyed before the channel nulls the
//     impl's insert_dsp instead of leaving it dangling.
//   - Interface-level setDSP()/getDSP() round-trip on a live channel.
//
// The behavioural cases drive CHANNEL::implementationObject directly (the same
// pattern the motif/player tests use): setup() sizes `out`, we write a known
// signal into GetBuffers(), attach a module via parseMessage(ATTACH_DSP), then
// call processInsertDSP() — exactly what dsp() invokes pre-fader — and assert
// on the resulting buffers. Engine-dependent cases guard on engineInit().

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include <vector>
#include "channel/channelInterface.hpp"
#include "channel/channelImplementation.h"
#include "channel/channelManager.h"
#include "channel/channelMessage.h"
#include "dsp/dspObject.hpp"
#include "sound/soundManager.h"
#include "internal/time.h"
#include "support/null_device.hpp"

namespace {
  // A minimal N-channel-aware insert module: multiplies every sample of every
  // channel by `gain`. Honours the dspObject N-channel contract (processes all
  // of buffer[0..size-1], no per-channel history so nothing to fan out).
  struct GainDsp : YSE::DSP::dspObject {
    explicit GainDsp(float g) : gain(g) {}
    void create() override {}
    void process(std::vector<YSE::DSP::buffer>& buffer) override {
      for (auto& ch : buffer) {
        float* p = ch.getPtr();
        const UInt n = ch.getLength();
        for (UInt i = 0; i < n; ++i)
          p[i] *= gain;
      }
    }
    float gain;
  };

  // Build a channel impl whose `out` is sized to the device layout. Requires a
  // live engine (setup() reads CHANNEL::Manager().getNumberOfOutputs()).
  void primeImpl(YSE::CHANNEL::implementationObject& impl) {
    impl.setStatus(YSE::OBJECT_CREATED);
    impl.setup();
  }

  // Fill every channel of `out` with a constant value.
  void fill(std::vector<YSE::DSP::buffer>& bufs, float value) {
    for (auto& b : bufs) {
      float* p = b.getPtr();
      const UInt n = b.getLength();
      for (UInt i = 0; i < n; ++i)
        p[i] = value;
    }
  }

  // True if every sample of every channel equals `value`.
  bool allEqual(std::vector<YSE::DSP::buffer>& bufs, float value) {
    for (auto& b : bufs) {
      float* p = b.getPtr();
      const UInt n = b.getLength();
      for (UInt i = 0; i < n; ++i)
        if (p[i] != doctest::Approx(value)) return false;
    }
    return true;
  }

  void attach(YSE::CHANNEL::implementationObject& impl, YSE::DSP::dspObject* dsp) {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::ATTACH_DSP;
    m.ptrValue = dsp;
    impl.parseMessage(m);
  }

  void drainChannels(int iterations = 8) {
    for (int i = 0; i < iterations; ++i) {
      YSE::INTERNAL::Time().update();
      YSE::SOUND::Manager().update();
      YSE::CHANNEL::Manager().update();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
} // namespace

TEST_SUITE("channel") {

  // ─── Message id ───────────────────────────────────────────────────────────

  TEST_CASE("channel dsp: ATTACH_DSP id is distinct from the other channel IDs") {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::ATTACH_DSP;
    CHECK(m.ID == YSE::CHANNEL::ATTACH_DSP);
    CHECK(m.ID != YSE::CHANNEL::VOLUME);
    CHECK(m.ID != YSE::CHANNEL::MOVE);
    CHECK(m.ID != YSE::CHANNEL::VIRTUAL);
    CHECK(m.ID != YSE::CHANNEL::ATTACH_REVERB);
  }

  // ─── Pre-fader processing behaviour ────────────────────────────────────────

  TEST_CASE("channel dsp: insert module scales the summed channel signal") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();
    REQUIRE(bufs.size() > 0);

    fill(bufs, 0.5f);
    GainDsp doubler(2.0f);
    attach(impl, &doubler);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, 1.0f));
  }

  TEST_CASE("channel dsp: gain-inverting insert flips the summed signal (acceptance)") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();
    REQUIRE(bufs.size() > 0);

    fill(bufs, 0.25f);
    GainDsp inverter(-1.0f);
    attach(impl, &inverter);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, -0.25f));
  }

  TEST_CASE("channel dsp: no attached chain leaves the signal untouched") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();
    REQUIRE(bufs.size() > 0);

    fill(bufs, 0.3f);
    impl.processInsertDSP(); // insert_dsp == nullptr -> no-op
    CHECK(allEqual(bufs, 0.3f));
  }

  TEST_CASE("channel dsp: linked modules both process, in order") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();
    REQUIRE(bufs.size() > 0);

    fill(bufs, 0.1f);
    GainDsp a(2.0f);
    GainDsp b(3.0f);
    a.link(b); // chain: a -> b
    attach(impl, &a);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, 0.6f)); // 0.1 * 2 * 3
  }

  TEST_CASE("channel dsp: bypassed module passes the signal through unchanged") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();
    REQUIRE(bufs.size() > 0);

    fill(bufs, 0.4f);
    GainDsp doubler(2.0f);
    doubler.bypass(true);
    attach(impl, &doubler);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, 0.4f));
  }

  // ─── Attach / replace / detach discipline (calledfrom) ─────────────────────

  TEST_CASE("channel dsp: replacing a plugin clears the old plugin's calledfrom") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);

    GainDsp first(2.0f);
    GainDsp second(3.0f);
    attach(impl, &first);
    CHECK(first.calledfrom != nullptr);

    attach(impl, &second);
    CHECK(first.calledfrom == nullptr); // old back-reference severed
    CHECK(second.calledfrom != nullptr); // new one points into the impl

    // Only the second module is now in the chain.
    auto& bufs = impl.GetBuffers();
    fill(bufs, 0.5f);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, 1.5f)); // 0.5 * 3, not * 2 or * 6
  }

  TEST_CASE("channel dsp: detaching with nullptr severs the chain") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);

    GainDsp doubler(2.0f);
    attach(impl, &doubler);
    CHECK(doubler.calledfrom != nullptr);

    attach(impl, nullptr); // detach
    CHECK(doubler.calledfrom == nullptr);

    auto& bufs = impl.GetBuffers();
    fill(bufs, 0.5f);
    impl.processInsertDSP(); // chain empty -> no-op
    CHECK(allEqual(bufs, 0.5f));
  }

  TEST_CASE("channel dsp: plugin destroyed before the channel nulls insert_dsp (no UAF)") {
    if (!TestHelpers::engineInit()) return;
    YSE::CHANNEL::implementationObject impl(nullptr);
    primeImpl(impl);
    auto& bufs = impl.GetBuffers();

    {
      GainDsp scoped(2.0f);
      attach(impl, &scoped);
      CHECK(scoped.calledfrom != nullptr);
    } // ~GainDsp writes *calledfrom = nullptr -> impl.insert_dsp becomes nullptr

    // The now-empty chain must be a safe no-op, not a use-after-free.
    fill(bufs, 0.5f);
    impl.processInsertDSP();
    CHECK(allEqual(bufs, 0.5f));
  }

  // ─── Interface-level API round-trip ────────────────────────────────────────

  TEST_CASE("channel dsp: setDSP / getDSP round-trip on a live channel") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel c;
    c.create("dsp_iface_channel", YSE::ChannelMaster());
    drainChannels();
    CHECK(c.getDSP() == nullptr);

    GainDsp gain(2.0f);
    c.setDSP(&gain);
    CHECK(c.getDSP() == &gain);
    drainChannels();

    c.setDSP(nullptr); // detach so the stack module doesn't outlive the impl
    CHECK(c.getDSP() == nullptr);
    drainChannels();
  }

} // TEST_SUITE("channel")

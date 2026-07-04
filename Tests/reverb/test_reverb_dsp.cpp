// Tests for the YSE reverb subsystem (YseEngine/internal/reverbDSP + YseEngine/reverb/).
//
// Coverage:
//   - reverbMessage construction and field accessors
//   - reverb interface default state (no engine required)
//   - reverb preset assignment via setPreset() (engine required)
//   - reverbDSP behavioral tests: bypass, impulse decay, dry/wet, stereo decorrelation
//   - reverb zone creation/destruction lifecycle (engine required)
//
// Engine-dependent tests guard on TestHelpers::engineInit().  If System().init()
// fails (e.g. no PortAudio host APIs in CI), those cases return without asserting.
//
// reverbDSP tests drive the Freeverb DSP object directly (no engine init needed).
// The DSP object uses static module-level globals for its inner loop; tests run
// sequentially so there is no data-race concern.

#include <doctest/doctest.h>
#include <type_traits>
// Channel headers must precede reverbManager.h because the manager references
// CHANNEL::implementationObject without a forward declaration.
// types.hpp defines Bool/Flt/UInt etc. needed by channelMessage.h.
// channelMessage.h must come before channelImplementation.h so that lfQueue<messageObject>
// can be fully instantiated (messageObject is only forward-declared in channel.hpp).
#include "headers/types.hpp"
#include "channel/channelMessage.h"
#include "channel/channelImplementation.h"
#include "reverb/reverbInterface.hpp"
#include "reverb/reverbMessage.h"
#include "reverb/reverbManager.h"
#include "internal/reverbDSP.h"
#include "support/null_device.hpp"
#include "support/audio_helpers.hpp"
#include "headers/defines.hpp" // MULTICHANNELBUFFER macro

TEST_SUITE("reverb") {

  // ─── reverbMessage: construction and field accessors ─────────────────────────

  TEST_CASE("reverbMessage: POSITION stores vec3 values") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::POSITION;
    m.vecValue[0] = 1.0f;
    m.vecValue[1] = 2.0f;
    m.vecValue[2] = 3.0f;
    CHECK(m.ID == YSE::REVERB::POSITION);
    CHECK(m.vecValue[0] == doctest::Approx(1.0f));
    CHECK(m.vecValue[1] == doctest::Approx(2.0f));
    CHECK(m.vecValue[2] == doctest::Approx(3.0f));
  }

  TEST_CASE("reverbMessage: SIZE stores float value") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::SIZE;
    m.floatValue = 5.0f;
    CHECK(m.ID == YSE::REVERB::SIZE);
    CHECK(m.floatValue == doctest::Approx(5.0f));
  }

  TEST_CASE("reverbMessage: ROLLOFF stores float value") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::ROLLOFF;
    m.floatValue = 0.8f;
    CHECK(m.ID == YSE::REVERB::ROLLOFF);
    CHECK(m.floatValue == doctest::Approx(0.8f));
  }

  TEST_CASE("reverbMessage: ACTIVE stores bool value") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::ACTIVE;
    m.boolValue = true;
    CHECK(m.ID == YSE::REVERB::ACTIVE);
    CHECK(m.boolValue == true);
  }

  TEST_CASE("reverbMessage: ROOMSIZE stores float value") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::ROOMSIZE;
    m.floatValue = 0.7f;
    CHECK(m.ID == YSE::REVERB::ROOMSIZE);
    CHECK(m.floatValue == doctest::Approx(0.7f));
  }

  TEST_CASE("reverbMessage: DAMP stores float value") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::DAMP;
    m.floatValue = 0.4f;
    CHECK(m.ID == YSE::REVERB::DAMP);
    CHECK(m.floatValue == doctest::Approx(0.4f));
  }

  TEST_CASE("reverbMessage: DRY_WET stores dry and wet in vec pair") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::DRY_WET;
    m.vecValue[0] = 0.6f; // dry
    m.vecValue[1] = 0.4f; // wet
    CHECK(m.ID == YSE::REVERB::DRY_WET);
    CHECK(m.vecValue[0] == doctest::Approx(0.6f));
    CHECK(m.vecValue[1] == doctest::Approx(0.4f));
  }

  TEST_CASE("reverbMessage: MODULATION stores frequency and width") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::MODULATION;
    m.vecValue[0] = 3.5f; // frequency
    m.vecValue[1] = 20.0f; // width
    CHECK(m.ID == YSE::REVERB::MODULATION);
    CHECK(m.vecValue[0] == doctest::Approx(3.5f));
    CHECK(m.vecValue[1] == doctest::Approx(20.0f));
  }

  TEST_CASE("reverbMessage: REFLECTION stores index, time, and gain") {
    YSE::REVERB::messageObject m;
    m.ID = YSE::REVERB::REFLECTION;
    m.vecValue[0] = 2.0f; // reflection index
    m.vecValue[1] = 150.0f; // time
    m.vecValue[2] = 0.5f; // gain
    CHECK(m.ID == YSE::REVERB::REFLECTION);
    CHECK(m.vecValue[0] == doctest::Approx(2.0f));
    CHECK(m.vecValue[1] == doctest::Approx(150.0f));
    CHECK(m.vecValue[2] == doctest::Approx(0.5f));
  }

  TEST_CASE("reverbMessage: MESSAGE IDs are all distinct") {
    CHECK(YSE::REVERB::POSITION != YSE::REVERB::SIZE);
    CHECK(YSE::REVERB::SIZE != YSE::REVERB::ROLLOFF);
    CHECK(YSE::REVERB::ROLLOFF != YSE::REVERB::ACTIVE);
    CHECK(YSE::REVERB::ACTIVE != YSE::REVERB::ROOMSIZE);
    CHECK(YSE::REVERB::ROOMSIZE != YSE::REVERB::DAMP);
    CHECK(YSE::REVERB::DAMP != YSE::REVERB::DRY_WET);
    CHECK(YSE::REVERB::DRY_WET != YSE::REVERB::MODULATION);
    CHECK(YSE::REVERB::MODULATION != YSE::REVERB::REFLECTION);
  }

  // ─── reverb copy semantics (issue #192) ──────────────────────────────────────

  TEST_CASE("reverb: copy-assignment is deleted (issue #192)") {
    // A reverb owns a raw pimpl the engine tracks by interface identity.
    // Copy-assigning default-copies pimpl, aliasing two interfaces onto one
    // implementation — which the manager used to do on the audio thread
    // (`calculatedValues = globalReverb`), turning the impl's SPSC message
    // queue into a dual-producer queue. Copy-assignment must stay deleted so
    // this cannot recur. (This is also enforced at compile time: reinstating
    // the assignment would fail the static_assert below.)
    static_assert(!std::is_copy_assignable<YSE::reverb>::value,
                  "reverb must not be copy-assignable (issue #192)");
    CHECK_FALSE(std::is_copy_assignable<YSE::reverb>::value);
  }

  // ─── reverb interface default state (no engine required) ─────────────────────

  TEST_CASE("reverb: default-constructed reverb is not valid") {
    YSE::reverb r;
    CHECK_FALSE(r.isValid());
  }

  TEST_CASE("reverb: default is active") {
    YSE::reverb r;
    CHECK(r.getActive() == true);
  }

  TEST_CASE("reverb: default roomsize is 0.5") {
    YSE::reverb r;
    CHECK(r.getRoomSize() == doctest::Approx(0.5f));
  }

  TEST_CASE("reverb: default damping is 0.5") {
    YSE::reverb r;
    CHECK(r.getDamping() == doctest::Approx(0.5f));
  }

  TEST_CASE("reverb: default wet is 0.5") {
    YSE::reverb r;
    CHECK(r.getWet() == doctest::Approx(0.5f));
  }

  TEST_CASE("reverb: default dry is 0.5") {
    YSE::reverb r;
    CHECK(r.getDry() == doctest::Approx(0.5f));
  }

  TEST_CASE("reverb: default modulation frequency and width are zero") {
    YSE::reverb r;
    CHECK(r.getModulationFrequency() == doctest::Approx(0.0f));
    CHECK(r.getModulationWidth() == doctest::Approx(0.0f));
  }

  TEST_CASE("reverb: default position is origin") {
    YSE::reverb r;
    YSE::Pos p = r.getPosition();
    CHECK(p.x == doctest::Approx(0.0f));
    CHECK(p.y == doctest::Approx(0.0f));
    CHECK(p.z == doctest::Approx(0.0f));
  }

  // ─── reverb interface preset assignment (engine required) ─────────────────────
  // setPreset() calls setRoomSize()/setDamping() etc., which dereference pimpl.
  // All preset tests therefore require create() to be called first.

  TEST_CASE("reverb: creation and destruction does not crash") {
    if (!TestHelpers::engineInit()) return;
    {
      YSE::reverb r;
      r.create();
      CHECK(r.isValid());
    } // ~reverb() fires here
  }

  TEST_CASE("reverb: isValid is true after create") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    CHECK(r.isValid());
  }

  TEST_CASE("reverb: REVERB_OFF sets dry=1, wet=0, roomsize=0, damp=0") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_OFF);
    CHECK(r.getRoomSize() == doctest::Approx(0.0f));
    CHECK(r.getDamping() == doctest::Approx(0.0f));
    CHECK(r.getWet() == doctest::Approx(0.0f));
    CHECK(r.getDry() == doctest::Approx(1.0f));
  }

  TEST_CASE("reverb: REVERB_GENERIC sets mid-range parameters") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_GENERIC);
    CHECK(r.getRoomSize() == doctest::Approx(0.5f));
    CHECK(r.getDamping() == doctest::Approx(0.5f));
    CHECK(r.getWet() == doctest::Approx(0.4f));
    CHECK(r.getDry() == doctest::Approx(0.6f));
  }

  TEST_CASE("reverb: REVERB_ROOM sets expected parameters") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_ROOM);
    CHECK(r.getRoomSize() == doctest::Approx(0.3f));
    CHECK(r.getDamping() == doctest::Approx(0.8f));
    CHECK(r.getWet() == doctest::Approx(0.3f));
    CHECK(r.getDry() == doctest::Approx(0.7f));
  }

  TEST_CASE("reverb: REVERB_HALL sets expected parameters") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_HALL);
    CHECK(r.getRoomSize() == doctest::Approx(0.7f));
    CHECK(r.getDamping() == doctest::Approx(0.4f));
    CHECK(r.getWet() == doctest::Approx(0.5f));
    CHECK(r.getDry() == doctest::Approx(0.5f));
  }

  TEST_CASE("reverb: REVERB_CAVE sets roomsize to 1.0 and high wet") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_CAVE);
    CHECK(r.getRoomSize() == doctest::Approx(1.0f));
    CHECK(r.getWet() == doctest::Approx(0.7f));
    CHECK(r.getDry() == doctest::Approx(0.3f));
  }

  TEST_CASE("reverb: REVERB_SEWERPIPE sets modulation frequency and width") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb r;
    r.create();
    r.setPreset(YSE::REVERB_SEWERPIPE);
    CHECK(r.getModulationFrequency() == doctest::Approx(3.5f));
    CHECK(r.getModulationWidth() == doctest::Approx(20.0f));
  }

  TEST_CASE("reverb: Manager global reverb is active after engine init") {
    if (!TestHelpers::engineInit()) return;
    YSE::reverb& gr = YSE::REVERB::Manager().getGlobalReverb();
    CHECK(gr.getActive() == true);
  }

  // ─── reverbDSP behavioral tests (no engine required) ─────────────────────────
  //
  // These tests instantiate reverbDSP directly and call process() with a
  // MULTICHANNELBUFFER (= std::vector<DSP::buffer>).  Faders inside reverbDSP
  // interpolate from 0 to their target over ~344 update() calls.  To avoid
  // long settling loops, we use lint::set(target(), 0) — which is immediate —
  // to snap faders to their constructor-set targets before testing.

  namespace {

    // Snap all five faders to their current targets with zero interpolation steps.
    void settleFaders(YSE::INTERNAL::reverbDSP& verb) {
      verb._wetFader.set(verb._wetFader.target(), 0);
      verb._dryFader.set(verb._dryFader.target(), 0);
      verb._widthFader.set(verb._widthFader.target(), 0);
      verb._roomsizeFader.set(verb._roomsizeFader.target(), 0);
      verb._dampFader.set(verb._dampFader.target(), 0);
    }

    // Mean energy (mean of squared samples) across channel 0 of a MULTICHANNELBUFFER.
    float chanEnergy(MULTICHANNELBUFFER& buf) {
      float rms = TestHelpers::measureRms(buf[0]);
      return rms * rms;
    }

    // Mean energy across channel 1.
    float chanEnergy1(MULTICHANNELBUFFER& buf) {
      float rms = TestHelpers::measureRms(buf[1]);
      return rms * rms;
    }

    // Process n zero blocks through verb (1-channel), discarding output.
    // silence[0] is zeroed on every iteration because process() overwrites it with
    // the reverb output; reusing the modified buffer would create positive feedback.
    void drainBlocks(YSE::INTERNAL::reverbDSP& verb, int n) {
      MULTICHANNELBUFFER silence(1);
      for (int i = 0; i < n; ++i) {
        silence[0] = 0.0f;
        verb.process(silence);
      }
    }

  } // namespace

  TEST_CASE("reverbDSP: bypass leaves buffer unchanged") {
    YSE::INTERNAL::reverbDSP verb;
    verb.channels(1);
    verb.bypass(true);

    MULTICHANNELBUFFER buf(1);
    buf[0] = 0.5f;
    verb.process(buf);

    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.5f));
  }

  TEST_CASE("reverbDSP: empty channel list leaves buffer unchanged") {
    YSE::INTERNAL::reverbDSP verb;
    // channels() not called — verb.channel.empty() == true
    verb.bypass(false);

    MULTICHANNELBUFFER buf(1);
    buf[0] = 0.3f;
    verb.process(buf);

    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.3f));
  }

  TEST_CASE("reverbDSP: bypass getter/setter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    CHECK(verb.bypass() == false);
    verb.bypass(true);
    CHECK(verb.bypass() == true);
    verb.bypass(false);
    CHECK(verb.bypass() == false);
  }

  TEST_CASE("reverbDSP: freeze getter/setter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    CHECK(verb.freeze() == false);
    verb.freeze(true);
    CHECK(verb.freeze() == true);
    verb.freeze(false);
    CHECK(verb.freeze() == false);
  }

  TEST_CASE("reverbDSP: combFeedback getter/setter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.combFeedback(0.8f);
    CHECK(verb.combFeedback() == doctest::Approx(0.8f));
  }

  TEST_CASE("reverbDSP: allpassFeedback getter/setter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.allpassFeedback(0.6f);
    CHECK(verb.allpassFeedback() == doctest::Approx(0.6f));
  }

  TEST_CASE("reverbDSP: roomSize setter/getter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.roomSize(0.7f);
    // Snap fader to its new target so the getter reads the settled value.
    verb._roomsizeFader.set(verb._roomsizeFader.target(), 0);
    CHECK(verb.roomSize() == doctest::Approx(0.7f).epsilon(0.01f));
  }

  TEST_CASE("reverbDSP: damp setter/getter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.damp(0.3f);
    verb._dampFader.set(verb._dampFader.target(), 0);
    CHECK(verb.damp() == doctest::Approx(0.3f).epsilon(0.01f));
  }

  TEST_CASE("reverbDSP: wet setter/getter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.wet(0.8f);
    verb._wetFader.set(verb._wetFader.target(), 0);
    CHECK(verb.wet() == doctest::Approx(0.8f).epsilon(0.01f));
  }

  TEST_CASE("reverbDSP: dry setter/getter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.dry(0.4f);
    verb._dryFader.set(verb._dryFader.target(), 0);
    CHECK(verb.dry() == doctest::Approx(0.4f).epsilon(0.01f));
  }

  TEST_CASE("reverbDSP: width setter/getter round-trip") {
    YSE::INTERNAL::reverbDSP verb;
    verb.width(0.5f);
    verb._widthFader.set(verb._widthFader.target(), 0);
    CHECK(verb.width() == doctest::Approx(0.5f).epsilon(0.01f));
  }

  TEST_CASE("reverbDSP: 100% dry passes input through unchanged") {
    // With _dryFader=1.0 (raw unity gain) and _wetFader=0.0 the wet path
    // contributes nothing (_wet1 = 0) and the buffer is multiplied by 1.0.
    YSE::INTERNAL::reverbDSP verb;
    verb.channels(1);
    verb.bypass(false);
    verb._dryFader.set(1.0f, 0); // raw=1.0 → buffer * 1.0 (no attenuation)
    verb._wetFader.set(0.0f, 0); // raw=0.0 → _wet1=0, no reverb tail added

    MULTICHANNELBUFFER buf(1);
    buf[0] = 0.5f;
    verb.process(buf);

    float* ptr = buf[0].getPtr();
    for (unsigned i = 0; i < buf[0].getLength(); ++i)
      CHECK(ptr[i] == doctest::Approx(0.5f).epsilon(1e-4f));
  }

  TEST_CASE("reverbDSP: impulse response produces non-zero reverb tail") {
    // Default constructor: wet target=1.0 (raw), dry target=0.0, width=1.0.
    // After settling, _wet1=1.0 so output is reverb tail only; input is zeroed.
    YSE::INTERNAL::reverbDSP verb;
    verb.channels(1);
    verb.bypass(false);
    settleFaders(verb);

    // Feed an impulse into the reverb.
    MULTICHANNELBUFFER impulse(1);
    impulse[0] = 0.0f;
    impulse[0].getPtr()[0] = 1.0f;
    verb.process(impulse);

    // Collect energy over blocks once the comb filters have started responding.
    // The shortest comb delay is ~1116 samples ≈ 9 blocks at STANDARD_BUFFERSIZE=128;
    // we start measuring from block 10 to avoid the silent pre-delay window.
    float tailEnergy = 0.0f;
    MULTICHANNELBUFFER silence(1);
    for (int i = 0; i < 60; ++i) {
      silence[0] = 0.0f; // reset each iteration — process() overwrites this buffer
      verb.process(silence);
      if (i >= 10) tailEnergy += chanEnergy(silence);
    }

    CHECK(tailEnergy > 0.0f);
  }

  TEST_CASE("reverbDSP: impulse response energy decays over time") {
    // Same setup as above — with settled faders (wet=1, dry=0) the output is
    // the reverb tail.  Its energy must decrease from early to late blocks.
    YSE::INTERNAL::reverbDSP verb;
    verb.channels(1);
    verb.bypass(false);
    settleFaders(verb);

    MULTICHANNELBUFFER impulse(1);
    impulse[0] = 0.0f;
    impulse[0].getPtr()[0] = 1.0f;
    verb.process(impulse);

    // Let the reverb build up past the pre-delay window.
    drainBlocks(verb, 15);

    // Measure energy over 40 blocks while the reverb is active.
    // silence[0] must be zeroed on every iteration: process() overwrites it with
    // the reverb output, and reusing that as input would create positive feedback.
    float earlyEnergy = 0.0f;
    MULTICHANNELBUFFER silence(1);
    for (int i = 0; i < 40; ++i) {
      silence[0] = 0.0f;
      verb.process(silence);
      earlyEnergy += chanEnergy(silence);
    }

    // Let the reverb decay substantially (combFeedback=0.84 → ~360 comb cycles
    // for 60 dB of decay; each cycle is ~1116 samples ≈ 9 blocks → ~3240 blocks).
    // We only wait 280 more blocks: enough to demonstrate clear decay without
    // making the test too slow.
    drainBlocks(verb, 280);

    // Measure energy over another 40 blocks — must be less than earlyEnergy.
    float lateEnergy = 0.0f;
    for (int i = 0; i < 40; ++i) {
      silence[0] = 0.0f;
      verb.process(silence);
      lateEnergy += chanEnergy(silence);
    }

    CHECK(lateEnergy < earlyEnergy);
  }

  TEST_CASE("reverbDSP: stereo channels produce decorrelated output") {
    // Each reverbChannel constructor calls Random(50) independently, giving the
    // two channels slightly different comb-filter tunings with ~98% probability.
    // After an impulse the L/R outputs therefore differ.
    YSE::INTERNAL::reverbDSP verb;
    verb.channels(2);
    verb.bypass(false);

    // Settle faders for the 2-channel case: the faders are shared, not per-channel.
    verb._wetFader.set(verb._wetFader.target(), 0);
    verb._dryFader.set(verb._dryFader.target(), 0);
    verb._widthFader.set(verb._widthFader.target(), 0);
    verb._roomsizeFader.set(verb._roomsizeFader.target(), 0);
    verb._dampFader.set(verb._dampFader.target(), 0);

    // Feed the same impulse to both channels.
    MULTICHANNELBUFFER impulse(2);
    impulse[0] = 0.0f;
    impulse[1] = 0.0f;
    impulse[0].getPtr()[0] = 1.0f;
    impulse[1].getPtr()[0] = 1.0f;
    verb.process(impulse);

    // Let comb filters build up past the pre-delay.
    for (int i = 0; i < 15; ++i) {
      MULTICHANNELBUFFER s(2);
      s[0] = 0.0f;
      s[1] = 0.0f;
      verb.process(s);
    }

    // Accumulate difference energy between L and R across 40 blocks.
    float diffEnergy = 0.0f;
    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    for (int i = 0; i < 40; ++i) {
      MULTICHANNELBUFFER s(2);
      s[0] = 0.0f;
      s[1] = 0.0f;
      verb.process(s);

      leftEnergy += chanEnergy(s);
      rightEnergy += chanEnergy1(s);

      float* l = s[0].getPtr();
      float* r = s[1].getPtr();
      unsigned len = s[0].getLength();
      for (unsigned k = 0; k < len; ++k) {
        float d = l[k] - r[k];
        diffEnergy += d * d;
      }
    }

    // Both channels must carry reverb energy.
    CHECK(leftEnergy > 0.0f);
    CHECK(rightEnergy > 0.0f);
    // L and R should differ because of independent Random() tuning offsets.
    // Identical tuning has ~2% probability (rnd = rand()%50 same twice in a row).
    CHECK_MESSAGE(diffEnergy > 0.0f,
                  "L/R channels should differ due to independent Random(50) comb-tuning offsets; "
                  "identical offsets occur with ~2% probability");
  }

} // TEST_SUITE("reverb")

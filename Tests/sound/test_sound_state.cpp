// Tests for the YSE sound subsystem (YseEngine/sound/).
// Covers: soundMessage accessors, sound default interface state, DSP-source
// lifecycle, parameter getter/setter round-trips, and file loading via a small
// WAV fixture.
//
// Engine-dependent tests guard on TestHelpers::engineInit().  If System().init()
// fails (e.g. no PortAudio host APIs), those cases return without asserting,
// which doctest counts as a pass.  Engine state is initialised once and shared
// across all test cases in this process (static singletons).
//
// Sounds created from a DSP source do not require file I/O or a live audio
// device.  They remain in the sound manager's implementation list until process
// exit; their destructor paths are safe because the interface calls
// removeInterface() (setting head = nullptr) before going out of scope.

#include <doctest/doctest.h>
#include "sound/soundInterface.hpp"
#include "sound/soundMessage.h"
#include "dsp/dspObject.hpp"
#include "support/null_device.hpp"

// Minimal no-op DSP source used to create sounds without file I/O.
namespace {
    struct SilentSource : YSE::DSP::dspSourceObject {
        void process(YSE::SOUND_STATUS&) override {}
        void frequency(float) override {}
    };
} // namespace

// Absolute path to the WAV fixture injected by CMake; falls back to a
// relative path that works when the test binary runs from build-X/bin/.
#ifndef YSE_TEST_FIXTURES_DIR
#  define YSE_TEST_FIXTURES_DIR "../../Tests/support/fixtures"
#endif
static const char* const WAV_FIXTURE =
    YSE_TEST_FIXTURES_DIR "/test_mono_44100.wav";

TEST_SUITE("sound") {

// ─── soundMessage: construction and field accessors ──────────────────────────

TEST_CASE("soundMessage: VOLUME_VALUE message stores float value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::VOLUME_VALUE;
    m.floatValue = 0.75f;
    CHECK(m.ID == YSE::SOUND::VOLUME_VALUE);
    CHECK(m.floatValue == doctest::Approx(0.75f));
}

TEST_CASE("soundMessage: SPEED message stores float value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::SPEED;
    m.floatValue = 2.0f;
    CHECK(m.ID == YSE::SOUND::SPEED);
    CHECK(m.floatValue == doctest::Approx(2.0f));
}

TEST_CASE("soundMessage: SIZE message stores float value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::SIZE;
    m.floatValue = 5.0f;
    CHECK(m.ID == YSE::SOUND::SIZE);
    CHECK(m.floatValue == doctest::Approx(5.0f));
}

TEST_CASE("soundMessage: SPREAD message stores float value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::SPREAD;
    m.floatValue = 0.3f;
    CHECK(m.ID == YSE::SOUND::SPREAD);
    CHECK(m.floatValue == doctest::Approx(0.3f));
}

TEST_CASE("soundMessage: LOOP message stores bool value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::LOOP;
    m.boolValue = true;
    CHECK(m.ID == YSE::SOUND::LOOP);
    CHECK(m.boolValue == true);
}

TEST_CASE("soundMessage: INTENT message stores SI_PLAY") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::INTENT;
    m.intentValue = YSE::SI_PLAY;
    CHECK(m.ID == YSE::SOUND::INTENT);
    CHECK(m.intentValue == YSE::SI_PLAY);
}

TEST_CASE("soundMessage: INTENT message stores SI_STOP") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::INTENT;
    m.intentValue = YSE::SI_STOP;
    CHECK(m.intentValue == YSE::SI_STOP);
}

TEST_CASE("soundMessage: INTENT message stores SI_PAUSE") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::INTENT;
    m.intentValue = YSE::SI_PAUSE;
    CHECK(m.intentValue == YSE::SI_PAUSE);
}

TEST_CASE("soundMessage: INTENT message stores SI_RESTART") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::INTENT;
    m.intentValue = YSE::SI_RESTART;
    CHECK(m.intentValue == YSE::SI_RESTART);
}

TEST_CASE("soundMessage: INTENT message stores SI_TOGGLE") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::INTENT;
    m.intentValue = YSE::SI_TOGGLE;
    CHECK(m.intentValue == YSE::SI_TOGGLE);
}

TEST_CASE("soundMessage: POSITION message stores vec3 values") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::POSITION;
    m.vecValue[0] = 1.0f;
    m.vecValue[1] = 2.0f;
    m.vecValue[2] = 3.0f;
    CHECK(m.ID == YSE::SOUND::POSITION);
    CHECK(m.vecValue[0] == doctest::Approx(1.0f));
    CHECK(m.vecValue[1] == doctest::Approx(2.0f));
    CHECK(m.vecValue[2] == doctest::Approx(3.0f));
}

TEST_CASE("soundMessage: DSP message stores pointer value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::DSP;
    void* sentinel = &m;
    m.ptrValue = sentinel;
    CHECK(m.ID == YSE::SOUND::DSP);
    CHECK(m.ptrValue == sentinel);
}

TEST_CASE("soundMessage: MOVE message stores pointer value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::MOVE;
    void* sentinel = &m;
    m.ptrValue = sentinel;
    CHECK(m.ID == YSE::SOUND::MOVE);
    CHECK(m.ptrValue == sentinel);
}

TEST_CASE("soundMessage: VOLUME_TIME message stores uint value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::VOLUME_TIME;
    m.uintValue = 500u;
    CHECK(m.ID == YSE::SOUND::VOLUME_TIME);
    CHECK(m.uintValue == 500u);
}

TEST_CASE("soundMessage: FADE_AND_STOP message stores uint value") {
    YSE::SOUND::messageObject m;
    m.ID = YSE::SOUND::FADE_AND_STOP;
    m.uintValue = 250u;
    CHECK(m.ID == YSE::SOUND::FADE_AND_STOP);
    CHECK(m.uintValue == 250u);
}

TEST_CASE("soundMessage: bool union field round-trips correctly") {
    YSE::SOUND::messageObject m;
    m.boolValue = true;
    CHECK(m.boolValue == true);
    m.boolValue = false;
    CHECK(m.boolValue == false);
}

TEST_CASE("soundMessage: MESSAGE IDs are all distinct") {
    CHECK(YSE::SOUND::POSITION      != YSE::SOUND::SPREAD);
    CHECK(YSE::SOUND::SPREAD        != YSE::SOUND::VOLUME_VALUE);
    CHECK(YSE::SOUND::VOLUME_VALUE  != YSE::SOUND::SPEED);
    CHECK(YSE::SOUND::SPEED         != YSE::SOUND::SIZE);
    CHECK(YSE::SOUND::SIZE          != YSE::SOUND::LOOP);
    CHECK(YSE::SOUND::LOOP          != YSE::SOUND::INTENT);
    CHECK(YSE::SOUND::INTENT        != YSE::SOUND::DSP);
    CHECK(YSE::SOUND::DSP           != YSE::SOUND::MOVE);
    CHECK(YSE::SOUND::MOVE          != YSE::SOUND::FADE_AND_STOP);
    CHECK(YSE::SOUND::FADE_AND_STOP != YSE::SOUND::VOLUME_TIME);
}

// ─── Sound default interface state (no engine init required) ─────────────────

TEST_CASE("sound: default-constructed sound is not valid") {
    YSE::sound s;
    CHECK_FALSE(s.isValid());
}

TEST_CASE("sound: default volume is 0.0") {
    YSE::sound s;
    CHECK(s.volume() == doctest::Approx(0.0f));
}

TEST_CASE("sound: default speed is 1.0") {
    YSE::sound s;
    CHECK(s.speed() == doctest::Approx(1.0f));
}

TEST_CASE("sound: default size is 0.0") {
    YSE::sound s;
    CHECK(s.size() == doctest::Approx(0.0f));
}

TEST_CASE("sound: default spread is 0.0") {
    YSE::sound s;
    CHECK(s.spread() == doctest::Approx(0.0f));
}

TEST_CASE("sound: default looping is false") {
    YSE::sound s;
    CHECK(s.looping() == false);
}

TEST_CASE("sound: default relative is false") {
    YSE::sound s;
    CHECK(s.relative() == false);
}

TEST_CASE("sound: default doppler is true") {
    YSE::sound s;
    CHECK(s.doppler() == true);
}

TEST_CASE("sound: default pan2D is false") {
    YSE::sound s;
    CHECK(s.pan2D() == false);
}

TEST_CASE("sound: default occlusion is false") {
    YSE::sound s;
    CHECK(s.occlusion() == false);
}

// ─── DSP sound lifecycle ─────────────────────────────────────────────────────

TEST_CASE("sound: valid after create with DSP source") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    CHECK(s.isValid());
}

TEST_CASE("sound: initial playback status is stopped after DSP create") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    CHECK(s.isStopped());
    CHECK_FALSE(s.isPlaying());
    CHECK_FALSE(s.isPaused());
}

TEST_CASE("sound: isReady does not crash immediately after DSP create") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    // Setup is dispatched asynchronously; we only verify the call is safe.
    (void)s.isReady();
}

TEST_CASE("sound: intent methods do not crash on a valid DSP sound") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.play();
    s.pause();
    s.toggle();
    s.restart();
    s.stop();
    s.fadeAndStop(100u);
    CHECK(s.isValid());
}

TEST_CASE("sound: destructor of a DSP sound does not crash") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    {
        YSE::sound s;
        s.create(src);
        CHECK(s.isValid());
    } // ~sound() fires here
}

// ─── Parameter getter / setter round-trips ───────────────────────────────────

TEST_CASE("sound: volume setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.volume(0.75f);
    CHECK(s.volume() == doctest::Approx(0.75f));
}

TEST_CASE("sound: volume clamped to 1.0 when set above 1.0") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.volume(2.5f);
    CHECK(s.volume() == doctest::Approx(1.0f));
}

TEST_CASE("sound: volume clamped to 0.0 when set below 0.0") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.volume(-1.0f);
    CHECK(s.volume() == doctest::Approx(0.0f));
}

TEST_CASE("sound: volume 0.0 is accepted") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.volume(0.0f);
    CHECK(s.volume() == doctest::Approx(0.0f));
}

TEST_CASE("sound: speed setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.speed(2.0f);
    CHECK(s.speed() == doctest::Approx(2.0f));
}

TEST_CASE("sound: looping setter/getter round-trip - true") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.looping(true);
    CHECK(s.looping() == true);
}

TEST_CASE("sound: looping setter/getter round-trip - false") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.looping(true);
    s.looping(false);
    CHECK(s.looping() == false);
}

TEST_CASE("sound: size setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.size(5.0f);
    CHECK(s.size() == doctest::Approx(5.0f));
}

TEST_CASE("sound: spread setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.spread(0.5f);
    CHECK(s.spread() == doctest::Approx(0.5f));
}

TEST_CASE("sound: spread clamped to 1.0 when set above 1.0") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.spread(2.0f);
    CHECK(s.spread() == doctest::Approx(1.0f));
}

TEST_CASE("sound: relative setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.relative(true);
    CHECK(s.relative() == true);
}

TEST_CASE("sound: doppler setter/getter round-trip - false") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.doppler(false);
    CHECK(s.doppler() == false);
}

TEST_CASE("sound: occlusion setter/getter round-trip") {
    if (!TestHelpers::engineInit()) return;
    SilentSource src;
    YSE::sound s;
    s.create(src);
    s.occlusion(true);
    CHECK(s.occlusion() == true);
}

// ─── File loading via WAV fixture ────────────────────────────────────────────
// abstractSoundFile loading is async (dispatched to the thread pool by
// soundFile::create()); we only verify the sound is created successfully
// (pimpl != nullptr) without waiting for the READY state.

TEST_CASE("sound: valid after create with WAV fixture") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    CHECK(s.isValid());
}

TEST_CASE("sound: initial status is stopped after WAV file create") {
    if (!TestHelpers::engineInit()) return;
    YSE::sound s;
    s.create(WAV_FIXTURE);
    if (!s.isValid()) return; // file missing in this environment
    CHECK(s.isStopped());
}

TEST_CASE("sound: WAV file sound destructs cleanly") {
    if (!TestHelpers::engineInit()) return;
    {
        YSE::sound s;
        s.create(WAV_FIXTURE);
    } // ~sound() fires here
}

} // TEST_SUITE("sound")

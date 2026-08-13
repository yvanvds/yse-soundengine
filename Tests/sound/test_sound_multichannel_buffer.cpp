// Regression coverage for issue #658: a sound created from a MULTICHANNELBUFFER
// (std::vector<DSP::buffer>) must actually play.
//
// abstractSoundFile::create() had an early-return branch for the single-channel
// _audioBuffer source but none at all for _multiChannelBuffer, so a
// multichannel-buffer source fell through into the *file* loading path. Its
// `fileName` is empty, so loadNonStreaming() opened an invalid SndfileHandle and
// ended at FILESTATE::INVALID; implementationObject::setup() then took the
// INVALID branch and marked the object OBJECT_DELETE_PENDING. The sound was not
// merely silent (the #657 symptom for the single-buffer overload) — it was
// dropped and never reached OBJECT_READY. readNonInterleaved() has always had a
// fully implemented _multiChannelBuffer branch; it was simply unreachable.
//
// Two layers, matching the two levels the bug is observable at:
//
//   1. White-box, on INTERNAL::soundFile directly — the create() branch itself:
//      state, channels(), length(), the degenerate inputs, and a real read()
//      that proves the per-channel data comes back distinct.
//   2. User-visible, through the public YSE::sound / YSE::channel API with the
//      offline renderer — the sound reaches ready, its playhead advances and
//      real signal lands on its channel. This is the level the user actually
//      experiences the bug at, and it is the one that fails on the unpatched
//      engine with isReady() stuck at false forever.
//
// Fail-without-fix (verified against the unpatched engine): layer 1's state is
// INVALID rather than READY with channels() == 0, and layer 2 never leaves
// isReady() == false, so the playhead and the channel peak both stay at 0.
//
// The white-box half needs INTERNAL::soundFile, whose header is gated behind
// LIBSOUNDFILE_BACKEND (defined PRIVATE on yse_objects); Tests/CMakeLists.txt
// enables that gate for this TU, same treatment as test_sound_bufferrace.cpp.

#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include <vector>

#include "yse.hpp"
#include "dsp/buffer.hpp"
#include "channel/channelInterface.hpp"
#include "sound/soundInterface.hpp"
#include "support/null_device.hpp"
#include "support/timer_pacing.hpp"

#if LIBSOUNDFILE_BACKEND
#include "internal/lsfSoundfile.h"
#endif

namespace {

  // Fill `buf` with a distinct per-channel DC-ish ramp so a mis-routed channel
  // is visible in the output rather than silently plausible. Channel c carries
  // values around (c + 1) * 0.25, which stays inside [-1, 1] for the channel
  // counts used here.
  void fillChannels(MULTICHANNELBUFFER& buf, UInt frames) {
    for (std::size_t c = 0; c < buf.size(); c++) {
      buf[c].resize(frames);
      Flt* p = buf[c].getPtr();
      const Flt level = 0.25f * static_cast<Flt>(c + 1);
      for (UInt n = 0; n < frames; n++)
        p[n] = level;
    }
  }

  // Source for the public-API case below. File scope (process lifetime) on
  // purpose: soundInterface.hpp's contract for the buffer overloads is that the
  // buffer must outlive the sound, and "the sound" here outlives the test case —
  // SOUND::Manager keeps the soundFile (which holds a raw MULTICHANNELBUFFER*)
  // in its dedup list until the slow-pool GC retires it ~30 s idle, and the impl
  // is released asynchronously. A stack-local source would be read after its
  // frame is gone. Long enough that the pump below cannot wrap it, so the
  // playhead is strictly monotonic and "it moved" is unambiguous.
  const UInt kPlaybackFrames = 16384;
  MULTICHANNELBUFFER g_playbackSource(2);

} // namespace

TEST_SUITE("sound") {

#if LIBSOUNDFILE_BACKEND

  TEST_CASE("soundFile: a MULTICHANNELBUFFER source becomes READY without loading (#658)") {
    if (!TestHelpers::engineInit()) return;

    const UInt frames = 512;
    MULTICHANNELBUFFER src(3);
    fillChannels(src, frames);

    YSE::INTERNAL::soundFile sf(&src);
    REQUIRE(sf.create(false));

    // Pre-fix this was INVALID: create() had no branch for _multiChannelBuffer,
    // so the file loader ran against an empty file name.
    const bool ready = (sf.getState() == YSE::INTERNAL::FILESTATE::READY);
    CHECK(ready);
    // Pre-fix _channels stayed at its constructor value of 0, which is what made
    // implementationObject::create()'s filebuffer.resize(file->channels()) leave
    // the sound with no output buffers (the #657 defect, in its sibling form).
    CHECK(sf.channels() == 3);
    CHECK(sf.length() == frames);
  }

  TEST_CASE("soundFile: a MULTICHANNELBUFFER source renders each channel (#658)") {
    if (!TestHelpers::engineInit()) return;

    const UInt frames = 512;
    MULTICHANNELBUFFER src(2);
    fillChannels(src, frames);

    YSE::INTERNAL::soundFile sf(&src);
    REQUIRE(sf.create(false));
    REQUIRE(sf.getState() == YSE::INTERNAL::FILESTATE::READY);

    // read() takes an output buffer per source channel — exactly what
    // implementationObject sizes from channels().
    const UInt blockSize = 128;
    std::vector<YSE::DSP::buffer> out(static_cast<std::size_t>(sf.channels()));
    for (auto& b : out)
      b.resize(blockSize);

    Flt pos = 0.f;
    Flt volume = 1.f;
    YSE::SOUND_STATUS intent = YSE::SS_PLAYING_FULL_VOLUME;
    REQUIRE(sf.read(out, pos, blockSize, 1.0f, /*loop=*/false, intent, volume));

    // The playhead advanced by exactly one block, and each output carries its
    // own source channel's level rather than a copy of channel 0 or silence.
    CHECK(pos == doctest::Approx(static_cast<Flt>(blockSize)));
    CHECK(out[0].getPtr()[0] == doctest::Approx(0.25f));
    CHECK(out[1].getPtr()[0] == doctest::Approx(0.50f));
    CHECK(out[0].getPtr()[blockSize - 1] == doctest::Approx(0.25f));
    CHECK(out[1].getPtr()[blockSize - 1] == doctest::Approx(0.50f));
  }

  TEST_CASE("soundFile: a MULTICHANNELBUFFER source reports the shortest channel (#658)") {
    if (!TestHelpers::engineInit()) return;

    // Ragged channels are legal input. read() resolves each channel's own length
    // per channel, but the reported length bounds a setFilePos seek, so it must
    // be the shortest — a seek clamped to the longer channel would index past
    // the end of the shorter one.
    MULTICHANNELBUFFER src(2);
    src[0].resize(800);
    src[1].resize(300);

    YSE::INTERNAL::soundFile sf(&src);
    REQUIRE(sf.create(false));
    CHECK(sf.getState() == YSE::INTERNAL::FILESTATE::READY);
    CHECK(sf.channels() == 2);
    CHECK(sf.length() == 300);
  }

  TEST_CASE("soundFile: a degenerate MULTICHANNELBUFFER source is rejected as INVALID (#658)") {
    if (!TestHelpers::engineInit()) return;

    // An empty vector has no channels at all: accepting it would reproduce the
    // zero-output-buffer defect. INVALID is the same verdict an unreadable file
    // gets, so setup() drops the sound cleanly instead of playing nothing.
    {
      MULTICHANNELBUFFER empty;
      YSE::INTERNAL::soundFile sf(&empty);
      REQUIRE(sf.create(false)); // still true — a false turns into a null soundFile
      const bool invalid = (sf.getState() == YSE::INTERNAL::FILESTATE::INVALID);
      CHECK(invalid);
    }

    // A zero-length channel is equally unplayable: the read loop would index an
    // empty buffer and its recalibration would never terminate.
    {
      MULTICHANNELBUFFER ragged(2);
      ragged[0].resize(256);
      ragged[1].resize(0); // DSP::buffer defaults to STANDARD_BUFFERSIZE, not 0
      YSE::INTERNAL::soundFile sf(&ragged);
      REQUIRE(sf.create(false));
      const bool invalid = (sf.getState() == YSE::INTERNAL::FILESTATE::INVALID);
      CHECK(invalid);
    }
  }

#endif // LIBSOUNDFILE_BACKEND

  // ─── User-visible: the public sound API, rendered offline ────────────────────
  //
  // Driven device-independently the same way the #212 gain-cache case is:
  // engineInit() pauses the audio stream so the test thread is the sole driver,
  // System().update() flags a control tick and renderOffline() runs real blocks
  // through the whole mix path onto a dedicated channel. There is no C API entry
  // point for the multichannel overload (yse_sound_load_buffer only wraps the
  // single-channel DSP::buffer version), so this C++ surface is the user-visible
  // level for this bug.
  TEST_CASE("sound: a MULTICHANNELBUFFER-backed sound plays instead of being dropped (#658)") {
    if (!TestHelpers::engineInit()) return;

    const UInt frames = kPlaybackFrames;
    fillChannels(g_playbackSource, frames);

    // Dedicated child channel so the peak below reflects only this sound.
    YSE::channel ch;
    ch.create("mcbuf658", YSE::ChannelMaster());

    YSE::sound s;
    s.create(g_playbackSource, &ch, /*loop=*/false, /*volume=*/0.8f);
    if (!s.isValid()) return;
    s.relative(true); // pan taken straight from position, listener ignored
    s.doppler(false); // playhead advances at exactly `speed`, no velocity term
    s.size(10.f); // near field: rolloff clamps to 1, so the gain is a clean pan
    s.pos(YSE::Pos(0.f, 0.f, 1.f)); // straight ahead — audible on every layout
    s.play();

    // Pump update+render until the async slow-pool setup promotes the sound.
    // Pre-fix this never happened: the file went INVALID and setup() marked the
    // object OBJECT_DELETE_PENDING, so isReady() stayed false however long we
    // waited. The budget is counted in ticks of the suite's pacing reference
    // rather than in wall clock, so it stretches with the load the slow pool is
    // under instead of expiring on it (issue #753).
    TestHelpers::pacedPump(
        3000, [&] { return s.isReady(); },
        [] {
          YSE::System().update();
          YSE::System().renderOffline(2);
        },
        5);
    REQUIRE(s.isReady());
    CHECK(s.length() == frames);

    // Now render for real and watch the two things the bug made unreachable:
    // the playhead advances, and signal reaches the channel.
    for (int i = 0; i < 20; i++) {
      YSE::System().update();
      YSE::System().renderOffline(2);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    CHECK(s.isPlaying());
    const float pos = s.time();
    CHECK(pos > 0.f);
    CHECK(pos < static_cast<float>(frames)); // no wrap — still the first pass

    const float peak = ch.getPeakLinearPre(0);
    CHECK(peak > 0.01f);

    s.stop();
  }

} // TEST_SUITE("sound")

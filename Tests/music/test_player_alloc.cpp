// RT-allocation test for the generative player note generator (issue #195).
//
// PLAYER::Manager().update() runs on the audio callback every block, and it
// calls implementationObject::update() for each live player. Before #195 that
// path did heap traffic proportional to musical activity: std::deque erase /
// insert of notes, voices.emplace_back when polyphony grew, whole-motif copies
// into each voice, and motifs.emplace_back / erase in parseMessage. This test
// drives the exact function Manager().update() invokes and asserts the steady
// state generates notes without allocating.
//
// The player subsystem has no live create() path yet (it was designed to hand
// notes to the not-yet-exposed synth), so the test constructs the
// implementation object directly with a player head — the same object the
// manager holds — and pumps update() from the test thread. This mirrors how the
// motif / scale impl tests in test_motif.cpp / test_scale.cpp drive their
// implementation objects, and needs no audio device (RandomF, the interpolators
// and the scale / motif lookups have no PortAudio dependency).
//
// The probe covers generation from a cold start (no warm-up), so the growth
// allocations the fix eliminates are in scope: the deque growing from empty,
// the per-voice motif buffers filling for the first time, and the motif pool
// growing in parseMessage. All config messages are sent before the probe opens
// so the message-queue traffic is excluded; the probed region is pure
// generation. Pre-#195 this fails (unbounded deque / vector growth allocates);
// after the preallocated-pool conversion the probed region sees zero
// allocations.

#include <doctest/doctest.h>

// Message headers must precede the impl headers: the inline sendMessage() in
// each impl calls lfQueue<messageObject>::push, which needs the complete
// messageObject type to size its storage.
#include "music/scale/scaleMessage.h"
#include "music/motif/motifMessage.h"
#include "music/scale/scaleInterface.hpp"
#include "music/scale/scaleImplementation.h"
#include "music/scale/scaleManager.h"
#include "music/motif/motifInterface.hpp"
#include "music/motif/motifImplementation.h"
#include "music/motif/motifManager.h"
#include "music/pNote.hpp"
#include "player/playerMessage.h"
#include "player/playerImplementation.h"
#include "player/playerInterface.hpp"

#include "support/alloc_probe.hpp"

namespace {

  // One audio block at the standard rate — the delta the manager feeds update().
  // SAMPLERATE is a runtime global, so this is a plain (non-constexpr) helper.
  Flt blockDelta() {
    return static_cast<Flt>(YSE::STANDARD_BUFFERSIZE) / static_cast<Flt>(YSE::SAMPLERATE);
  }

  // Build a small, populated motif via the real interface + manager path.
  void fillMotif(YSE::motif& m) {
    m.add(YSE::MUSIC::pNote(0.00f, 60.f, 0.8f, 0.05f));
    m.add(YSE::MUSIC::pNote(0.05f, 64.f, 0.8f, 0.05f));
    m.add(YSE::MUSIC::pNote(0.10f, 67.f, 0.8f, 0.05f));
    m.add(YSE::MUSIC::pNote(0.15f, 72.f, 0.8f, 0.05f));
    m.setLength();
    YSE::MOTIF::Manager().update(); // drain the ADD / LENGTH messages into the impl
  }

} // namespace

TEST_SUITE("music") {

  TEST_CASE("player: pure-random note generation does not allocate on the audio path (#195)") {
    YSE::player head;
    YSE::PLAYER::implementationObject impl(&head);

    // Aggressive, allocation-hungry configuration: full polyphony, wide pitch
    // range, and very short notes with no gaps so the generator churns the note
    // pool as fast as possible.
    head.setVoices(YSE::PLAYER::MAX_VOICES);
    head.setMinimumPitch(24.f);
    head.setMaximumPitch(96.f);
    head.setMinimumVelocity(0.2f);
    head.setMaximumVelocity(0.9f);
    head.setMinimumGap(0.f);
    head.setMaximumGap(0.f);
    head.setMinimumLength(0.01f);
    head.setMaximumLength(0.05f);
    head.play();

    // Probe from a cold start: every config message is already queued, so the
    // probed region is pure generation — the exact path that used to grow the
    // note pool from empty. update() drains the queue (try_pop never allocates)
    // and then generates into the ctor-reserved pools.
    const Flt delta = blockDelta();
    bool generated = false;
    UInt peakNotes = 0;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 4000; ++i) {
        impl.update(delta);
        UInt n = impl.noteCount();
        if (n > 0u) generated = true;
        if (n > peakNotes) peakNotes = n;
      }
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
    CHECK(generated); // the generator actually produced notes
    CHECK(peakNotes <= YSE::PLAYER::MAX_NOTES); // the fixed ceiling held throughout
  }

  TEST_CASE("player: motif + scale note generation does not allocate on the audio path (#195)") {
    // A scale to constrain / quantise pitches, populated through the real path.
    YSE::scale sc;
    sc.add(60.f, 12.f);
    sc.add(62.f, 12.f);
    sc.add(64.f, 12.f);
    sc.add(67.f, 12.f);
    sc.add(69.f, 12.f);
    YSE::SCALE::Manager().update();

    // Two weighted motifs the player draws from.
    YSE::motif m1;
    YSE::motif m2;
    fillMotif(m1);
    fillMotif(m2);

    YSE::player head;
    YSE::PLAYER::implementationObject impl(&head);

    head.setVoices(YSE::PLAYER::MAX_VOICES);
    head.setMinimumPitch(36.f);
    head.setMaximumPitch(84.f);
    head.setMinimumVelocity(0.2f);
    head.setMaximumVelocity(0.9f);
    head.setMinimumGap(0.f);
    head.setMaximumGap(0.01f);
    head.setMinimumLength(0.01f);
    head.setMaximumLength(0.05f);
    head.setScale(sc);
    head.addMotif(m1, 2);
    head.addMotif(m2, 1);
    head.playMotifs(1.f); // always draw from motifs when a note starts
    head.playPartialMotifs(0.5f); // mix full and partial motif copies
    head.fitMotifsToScale(1.f); // quantise every motif note to the scale
    head.play();

    // Probe from cold: the first motif selection copies a whole motif into each
    // voice's buffer and parseMessage appends to the motif pool — both grew the
    // heap before #195. The ctor-reserved buffers must absorb all of it.
    const Flt delta = blockDelta();
    bool generated = false;
    UInt peakNotes = 0;
    {
      TestHelpers::ProbeScope probe;
      for (int i = 0; i < 4000; ++i) {
        impl.update(delta);
        UInt n = impl.noteCount();
        if (n > 0u) generated = true;
        if (n > peakNotes) peakNotes = n;
      }
    }
    CHECK(TestHelpers::g_alloc_count.load() == 0);
    CHECK(generated);
    CHECK(peakNotes <= YSE::PLAYER::MAX_NOTES);
  }

} // TEST_SUITE("music")

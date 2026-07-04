// RT-allocation regression tests for YSE::virtualFinder (issue #194).
//
// virtualFinder::reset()/add()/calculate() all run on the audio callback thread
// (SOUND::Manager::update() calls reset()+calculate(); soundImplementation::dsp()
// calls add()). The old implementation grew/shrank its `bin` vector with
// bin.resize() inside calculate(), which reallocated on the heap — and, because
// the resolution was unbounded, could resize on essentially every callback under
// sustained load. The fix pins the backing buffer to a fixed maximum size and
// only moves the logical resolution within [MIN, MAX], so the histogram never
// touches the heap after construction.
//
// The functional behavior of inRange()/calculate() is covered in
// test_sound_impl.cpp; here we lock in the zero-allocation guarantee and the
// resolution clamp under a load pattern that reallocated (unboundedly) before.

#include <doctest/doctest.h>

#include "internal/virtualFinder.h"
#include "support/alloc_probe.hpp"

TEST_SUITE("sound") {

  TEST_CASE("virtualFinder: sustained reset/add/calculate cycles never allocate (#194)") {
    // Construct OUTSIDE the probe: the one-time bin allocation happens here.
    YSE::virtualFinder vf(10);
    vf.setLimit(10);

    // Warm up one cycle so any lazy first-use state is established pre-probe.
    vf.reset();
    for (int k = 0; k < 100; ++k)
      vf.add(0.5f);
    vf.calculate();

    {
      TestHelpers::ProbeScope probe;
      // This pattern (a big spike of entries into a single low bin, limit far
      // below the entry count) drove the old calculate() to grow the bin vector
      // every cycle without bound — hundreds of reallocations. With the fix the
      // resolution saturates at its clamp and the buffer is never resized.
      for (int cycle = 0; cycle < 400; ++cycle) {
        vf.reset();
        for (int k = 0; k < 100; ++k)
          vf.add(0.5f);
        vf.calculate();
      }
      // Also exercise the shrink branch: many cycles right at the limit.
      for (int cycle = 0; cycle < 400; ++cycle) {
        vf.reset();
        for (int k = 0; k < 11; ++k)
          vf.add(static_cast<float>(k));
        vf.calculate();
      }
    }

    CHECK(TestHelpers::g_alloc_count.load() == 0);
  }

  TEST_CASE("virtualFinder: stays functional after the resolution clamp saturates (#194)") {
    YSE::virtualFinder vf(10);
    vf.setLimit(4);

    // Drive the same 8-entry (distances 1..8) histogram for many cycles. With a
    // budget of 4 the count always overshoots the limit, so the old code would
    // have grown `resolution` without bound; the fix saturates it at the clamp.
    // Repeating the identical distribution also lets `calculatedMax` (carried
    // reset-to-reset) settle, so the histogram scale is stable when we assert.
    for (int cycle = 0; cycle < 200; ++cycle) {
      vf.reset();
      for (int k = 0; k < 8; ++k)
        vf.add(static_cast<float>(k + 1)); // 1..8
      vf.calculate();
    }

    // A very near sound is kept; a very far one is dropped, regardless of prior
    // real/virtual state. (The exact cutoff is covered elsewhere; this only
    // checks the finder didn't degenerate after the clamp.)
    CHECK(vf.inRange(0.5f, /*wasReal=*/false) == true);
    CHECK(vf.inRange(100.f, /*wasReal=*/true) == false);
  }

} // TEST_SUITE("sound")

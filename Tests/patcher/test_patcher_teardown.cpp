// Tests for the patcher's teardown stop pass (issue #758) — the thing that
// makes "a cleared or destroyed patcher leaves no hardware note sounding" true.
//
// What is being pinned is a *patcher* behaviour, not an object one, which is
// why it lives in its own file rather than in test_patcher_midiflush.cpp or
// test_patcher_makenote.cpp: neither object can implement it alone. The bug it
// closes is an ordering bug in `patcherImplementation::Clear`, which unwired
// each object as it walked, so an object reached after the `.midiout`
// downstream of it would have sent its releases into a cord that no longer
// existed. Teardown is now two passes — stop everything, then unwire and retire
// — and the hook is a virtual on `pObject` rather than a type-name check.
//
// The claims:
//
//   - **`.midiflush` flushes** when the patcher is cleared, when it is
//     destroyed, and when the object itself is deleted;
//   - **`.makenote` releases** its pending notes on all three of the same
//     routes — the acceptance item #538 could not satisfy on its own;
//   - **the release travels down real cords** to a real downstream object, and
//     is therefore something a `.midiout` would have sent;
//   - **the pass does not depend on the order objects are visited in**: a
//     `.makenote` -> `.noteon` -> `.midiflush` chain ends with nothing sounding
//     whichever end the pass reaches first, because every cord is intact for
//     the whole of it;
//   - **an object with nothing to release is untouched**, the hook being a
//     no-op by default;
//   - **`.metro` still stops**, which used to be the type-name special case the
//     virtual replaced.
//
// Every case that asserts "nothing is left sounding" is preceded by, or paired
// with, a control showing the rig can see a hanging note in the first place. A
// test that only ever checked for silence would pass against a chain that never
// sounded anything at all.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <cstddef>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObject.h"
#include "patcher/pObjectList.hpp"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/TimerThread.h"

using YSE::PATCHER::patcherImplementation;

namespace {

  // Records every message that arrived, in order and in the shape it arrived
  // in: an int as "i<n>" and a list as "l<text>". Both matter here — a
  // `.midiflush` releases as a numeric byte list and a `.makenote` as a pair of
  // ints — and a sink that normalised either could not tell one from the other.
  //
  // This object is deliberately *not* owned by the patcher: it stands in for
  // everything downstream of the patch that a note-off has to reach, which for
  // a real patch is `.midiout` and a device. Declared before the patcher in
  // every case below, so the patcher dies first and the pass has somewhere to
  // send (sinks.hpp's teardown rule).
  struct Tap : YSE::PATCHER::pObject {
    std::vector<std::string> log;

    Tap() : pObject(false) {
      log.reserve(64);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("i" + std::to_string(v)); });
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { log.push_back("l" + v); });

      // A second, cold inlet so a `.makenote`'s velocity outlet can be watched
      // too: the release is "the same pitch with velocity 0", and a rig that
      // only saw the pitch could not tell an attack from a release.
      inputs.emplace_back(this, false, 1);
      inputs.back().RegisterInt(
          [this](int v, int, YSE::THREAD) { log.push_back("v" + std::to_string(v)); });
    }
    const char* Type() const override {
      return "teardown_tap";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::string trace() const {
      std::string out;
      for (const std::string& entry : log) {
        if (!out.empty()) out.push_back(' ');
        out += entry;
      }
      return out;
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── .midiflush ───────────────────────────────────────────────────────────

  TEST_CASE("teardown: a cleared patcher flushes the notes it left sounding (#758)") {
    // The control and the claim in one case. `.midiformat` builds a real note-on
    // through a real cord into a real `.midiflush`, whose outlet is where a
    // `.midiout` would sit. Nothing releases the note, so before the Clear the
    // tap has seen one message and the device is holding a key down.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* format = p.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(format != nullptr);
    REQUIRE(flush != nullptr);
    p.Connect(format, 0, flush, 0);
    p.Connect(flush, 0, &tapHandle, 0);

    format->SetListData(0, "60 100");
    REQUIRE(tap.trace() == "l144 60 100");

    tap.log.clear();
    p.Clear();
    // The note-off the patch never sent, sent by the teardown pass — down the
    // cord that would have been gone had Clear unwired as it walked.
    CHECK(tap.trace() == "l128 60 0");
  }

  TEST_CASE("teardown: a destroyed patcher flushes too (#758)") {
    // ~patcherImplementation calls Clear(), so the destructor is the same route.
    // It is worth its own case because it is the one a host actually takes:
    // nobody clears a patcher on the way out, they drop it.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* format = p.CreateObject(YSE::OBJ::M_FORMAT, "");
      YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
      REQUIRE(format != nullptr);
      REQUIRE(flush != nullptr);
      p.Connect(format, 0, flush, 0);
      p.Connect(flush, 0, &tapHandle, 0);

      format->SetListData(0, "62 100");
      REQUIRE(tap.log.size() == 1);
      tap.log.clear();
    }

    CHECK(tap.trace() == "l128 62 0");
  }

  TEST_CASE("teardown: deleting a .midiflush on its own flushes it (#758)") {
    // DeleteObject is the second teardown route and has no Clear() to hook. An
    // object removed while it is holding notes still owes them their releases,
    // and the rest of the patch — the tap standing in for `.midiout` — is still
    // there to receive them.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* format = p.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(format != nullptr);
    REQUIRE(flush != nullptr);
    p.Connect(format, 0, flush, 0);
    p.Connect(flush, 0, &tapHandle, 0);

    format->SetListData(0, "64 100");
    format->SetListData(0, "67 100");
    REQUIRE(tap.log.size() == 2);
    tap.log.clear();

    p.DeleteObject(flush);
    CHECK(tap.trace() == "l128 64 0 l128 67 0");
  }

  TEST_CASE("teardown: a .midiflush with nothing sounding sends nothing (#758)") {
    // The other half of the claim, and the one that keeps the pass from being a
    // burst of spurious note-offs at every Clear: a flush releases exactly what
    // is sounding, which after a properly balanced phrase is nothing.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* format = p.CreateObject(YSE::OBJ::M_FORMAT, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(format != nullptr);
    REQUIRE(flush != nullptr);
    p.Connect(format, 0, flush, 0);
    p.Connect(flush, 0, &tapHandle, 0);

    format->SetListData(0, "60 100");
    format->SetListData(0, "60 0"); // a note-on with velocity 0 is a release
    REQUIRE(tap.log.size() == 2);
    tap.log.clear();

    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── .makenote ────────────────────────────────────────────────────────────

  TEST_CASE("teardown: a cleared patcher releases .makenote's pending notes (#758)") {
    // #538's acceptance item, which that issue could not satisfy on its own:
    // "all pending note-offs fire on object teardown — no hanging notes when a
    // patcher is deleted".
    //
    // The duration is long and the patcher never renders, so nothing falls due
    // on its own — the three notes really are sounding when the Clear arrives.
    // Without the pass they would not misfire, they would simply never end: the
    // scheduler drops a destroyed object's messages, and a dropped release is
    // the hanging note.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 60000");
    REQUIRE(mk != nullptr);
    p.Connect(mk, 1, &tapHandle, 1); // velocity, the cold inlet
    p.Connect(mk, 0, &tapHandle, 0); // pitch, the hot one

    mk->SetIntData(0, 60);
    mk->SetIntData(0, 64);
    REQUIRE(tap.trace() == "v100 i60 v100 i64");

    tap.log.clear();
    p.Clear();
    // Released in the order they were played, each with velocity 0 — which is
    // exactly what `stop` sends, because that is what the hook runs.
    CHECK(tap.trace() == "v0 i60 v0 i64");
  }

  TEST_CASE("teardown: a destroyed patcher releases .makenote's pending notes (#758)") {
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 60000");
      REQUIRE(mk != nullptr);
      p.Connect(mk, 1, &tapHandle, 1);
      p.Connect(mk, 0, &tapHandle, 0);

      mk->SetIntData(0, 67);
      REQUIRE(tap.trace() == "v100 i67");
      tap.log.clear();
    }

    CHECK(tap.trace() == "v0 i67");
  }

  TEST_CASE("teardown: deleting a .makenote on its own releases its notes (#758)") {
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 60000");
    REQUIRE(mk != nullptr);
    p.Connect(mk, 1, &tapHandle, 1);
    p.Connect(mk, 0, &tapHandle, 0);

    mk->SetIntData(0, 72);
    REQUIRE(tap.log.size() == 2);
    tap.log.clear();

    p.DeleteObject(mk);
    CHECK(tap.trace() == "v0 i72");
  }

  // ─── the whole chain, and the order the pass walks it in ──────────────────

  TEST_CASE("teardown: a .makenote -> .noteon -> .midiflush chain ends silent (#758)") {
    // The two objects that need the hook, in one chain, with the object that is
    // the honest judge of "is anything sounding" sitting at the end of it.
    //
    // The pass visits objects in an order this test does not control and cannot:
    // the patcher's object map is keyed by handle address. It does not need to,
    // because every cord is intact for the whole pass — but the two orders do
    // not produce the same *bytes*, and that is worth stating rather than
    // hiding:
    //
    //   - `.makenote` first: its releases run down the chain, `.midiflush`
    //     decodes them on the way past and so has nothing of its own left to
    //     send. Three messages, all binary note-on-velocity-0.
    //   - `.midiflush` first: it emits three numeric "128 p 0" note-offs for
    //     what is sounding *now*, and then `.makenote`'s own releases follow
    //     down the same chain. Six messages, each pitch released twice.
    //
    // A second note-off for a note the device has already let go is nothing —
    // that is the same idempotence `.midiflush` relies on to be safe to bang
    // twice. So the claim this case pins is the one a device would make, and
    // the only one true in both orders: after teardown every pitch that was
    // played has been released, and nothing that is not a release was sent.
    // A count or a fixed trace would be pinning the allocator.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* mk = p.CreateObject(YSE::OBJ::M_MAKENOTE, "100 60000");
    YSE::pHandle* noteon = p.CreateObject(YSE::OBJ::M_NOTEON, "");
    YSE::pHandle* flush = p.CreateObject(YSE::OBJ::M_MIDIFLUSH, "");
    REQUIRE(mk != nullptr);
    REQUIRE(noteon != nullptr);
    REQUIRE(flush != nullptr);

    p.Connect(mk, 1, noteon, 1); // velocity, cold
    p.Connect(mk, 0, noteon, 0); // pitch, hot — this is what sends
    p.Connect(noteon, 0, flush, 0);
    p.Connect(flush, 0, &tapHandle, 0);

    mk->SetIntData(0, 60);
    mk->SetIntData(0, 64);
    mk->SetIntData(0, 67);
    REQUIRE(tap.log.size() == 3); // three note-ons went down the stream

    // The control: three note-ons and nothing else have gone past, so three
    // notes really are hanging when the Clear arrives — which is what makes the
    // silence below evidence rather than an empty rig.
    tap.log.clear();

    p.Clear();
    REQUIRE(!tap.log.empty());

    // Both spellings a release can arrive in are decoded, because which one
    // turns up is exactly the visit-order question above: `.midiflush` emits its
    // own as the numeric list "128 60 0", while `.makenote`'s reach the tap as
    // the binary note-on-with-velocity-0 that `.noteon` built and `.midiflush`
    // passed through — a release in MIDI just as much as a 0x80 is.
    int released60 = 0;
    int released64 = 0;
    int released67 = 0;
    for (const std::string& entry : tap.log) {
      REQUIRE(entry.size() >= 2);
      REQUIRE(entry[0] == 'l');
      const std::string body = entry.substr(1);

      int bytes[3] = {0, 0, 0};
      if (body.size() == 3 && (unsigned char)body[0] >= 0x80) {
        for (int i = 0; i < 3; i++)
          bytes[i] = (int)(unsigned char)body[i];
      } else {
        std::size_t at = 0;
        for (int i = 0; i < 3; i++) {
          std::size_t used = 0;
          bytes[i] = std::stoi(body.substr(at), &used);
          at += used;
        }
      }

      const int kind = bytes[0] & 0xF0;
      const bool release = (kind == 0x80) || (kind == 0x90 && bytes[2] == 0);
      REQUIRE(release);
      if (bytes[1] == 60) released60++;
      if (bytes[1] == 64) released64++;
      if (bytes[1] == 67) released67++;
    }
    CHECK(released60 >= 1);
    CHECK(released64 >= 1);
    CHECK(released67 >= 1);
  }

  // ─── the hook is a no-op for everything else ──────────────────────────────

  TEST_CASE("teardown: an object with nothing to release sends nothing (#758)") {
    // pObject::Teardown is a no-op by default, so only the objects that need it
    // pay for it — and, more to the point here, a Clear does not spray a burst
    // of stimuli through a patch that had nothing outstanding. A `.int` holding
    // a value is the ordinary case: it has state, but none of it is sounding
    // anywhere outside the patch.
    Tap tap;
    YSE::pHandle tapHandle(&tap);

    patcherImplementation p(1, nullptr);
    YSE::pHandle* value = p.CreateObject(YSE::OBJ::G_INT, "");
    REQUIRE(value != nullptr);
    p.Connect(value, 0, &tapHandle, 0);

    value->SetIntData(0, 42);
    REQUIRE(tap.trace() == "i42");

    tap.log.clear();
    p.Clear();
    CHECK(tap.log.empty());
  }

  // ─── the special case the virtual replaced ────────────────────────────────

  TEST_CASE("teardown: a running .metro is still stopped by Clear (#758)") {
    // Until #758 this was a `handle->Type() == OBJ::G_METRO` check inside Clear
    // and DeleteObject that poked a 0 into inlet 0; it is now gMetro::Teardown,
    // which runs the same StopRun. The thing it protects is #663: a metro that
    // reaches its destructor with a live timer leaves the worker calling
    // SendBang() on a freed object. So the assertion is on the timer, not on
    // the bangs — the timer outliving the object *is* the bug.
    //
    // Two cases rather than one, and both new, because the old check turns out
    // never to have fired: it compared `const char*` **pointers**, and the
    // object tag it compared against is a `static constexpr char const*` on a
    // struct in a header, so the address `gMetro::Type()` returns is not
    // necessarily the address `patcherImplementation.cpp` was holding. Every
    // other type test in the engine uses `strcmp` for exactly that reason;
    // these two were the only `==` ones left. Nothing pinned them at the
    // patcher level, so the special case sat there reading as a guarantee while
    // doing nothing — which is the strongest argument there was for turning it
    // into a virtual, and the reason these cases fail against the code before
    // this change.
    const std::size_t before = YSE::PATCHER::TimerThread().size();

    {
      patcherImplementation p(1, nullptr);
      YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "5000");
      REQUIRE(metro != nullptr);
      metro->SetIntData(0, 1); // start
      CHECK(YSE::PATCHER::TimerThread().size() == before + 1);

      p.Clear();
      CHECK(YSE::PATCHER::TimerThread().size() == before);
    }
    CHECK(YSE::PATCHER::TimerThread().size() == before);
  }

  TEST_CASE("teardown: a running .metro is still stopped by DeleteObject (#758)") {
    const std::size_t before = YSE::PATCHER::TimerThread().size();

    patcherImplementation p(1, nullptr);
    YSE::pHandle* metro = p.CreateObject(YSE::OBJ::G_METRO, "5000");
    REQUIRE(metro != nullptr);
    metro->SetIntData(0, 1);
    CHECK(YSE::PATCHER::TimerThread().size() == before + 1);

    p.DeleteObject(metro);
    CHECK(YSE::PATCHER::TimerThread().size() == before);
  }

} // TEST_SUITE

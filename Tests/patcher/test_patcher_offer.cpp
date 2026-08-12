// Tests for `.offer` — Max's offer, which stores "one-time number pairs":
// "store two ints as an x, y pair, and access them by x value. When a pair is
// retrieved, it is deleted from the collection" (issue #544).
//
// The trap this object sets is that inlet 0 does two opposite things and
// nothing in the message says which. Whether an x *stores* a pair or
// *withdraws* one depends entirely on whether inlet 1 has armed a y since the
// last x — Max's "if a y value has been received in the right inlet, the two
// numbers are stored together in offer; otherwise, offer looks for an x value
// that matches". An implementation that armed on every y and never spent it
// would store forever and never answer; one that treated a y of 0 as "nothing
// armed" would pass every naive test and then break on the exact note stream
// Max designed the object for, since 0 is the velocity a note-off carries. So
// every case that checks something is sent is paired with one that checks
// something is *not*, and 0 gets a case of its own.
//
// Four layers, and they are not interchangeable:
//
//   - **standalone cases** pin the grammar and the shape: what each inlet
//     accepts, the store/withdraw split and what spends the armed y, that a
//     retrieval deletes, that a bang does not, what a list means, and what
//     `clear` reaches.
//
//   - **the store**, which is the object: duplicates, which pair a query takes,
//     the capacity refusal, and the re-entrancy the released guard allows.
//
//   - **end-to-end cases** run Max's own documented use case inside a real
//     patcher — a real `.midiparse` into a real `.unpack` into a real
//     `.trigger`, a real `.stripnote` gating a real `.+` into the y inlet — and
//     ask the question the issue asks: when the note-off arrives carrying the
//     pitch the *player* released, does the patch get back the pitch the synth
//     is actually sounding? Including after the transposition has changed
//     underneath it, which is the whole reason the mapping has to be stored per
//     note rather than recomputed.
//
//   - **real-time cases** measure the message paths for allocation on the
//     thread that runs them.
//
// No audio device and no MIDI hardware required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "patcher/inlet.h"
#include "patcher/midi/mOffer.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/sinks.hpp"
#include "support/alloc_probe.hpp"

using YSE::PATCHER::mOffer;
using YSE::PATCHER::Register;

namespace {

  // Records every int that leaves the outlet, in order. The order is half of
  // what a bang promises, and a sink that only kept the last value could not
  // tell a dump from a single send.
  //
  // The vector is reserved up front so the allocation probe measures the object
  // under test and not this sink.
  struct Log : YSE::PATCHER::pObject {
    std::vector<int> values;

    Log() : pObject(false) {
      values.reserve(4096);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { values.push_back(v); });
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) { values.push_back((int)v); });
    }
    const char* Type() const override {
      return "offer_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    std::size_t n() const {
      return values.size();
    }
    void Clear() {
      values.clear();
    }

    // "67 69" — everything that left the outlet, in the order it left.
    std::string trace() const {
      std::string out;
      for (int v : values) {
        if (!out.empty()) out.push_back(' ');
        out += std::to_string(v);
      }
      return out;
    }
  };

  using TestHelpers::Wire;

  // A standalone `.offer` with its outlet watched. The object needs no clock and
  // no patcher, so standalone is the whole of its message behaviour.
  //
  // The sink is declared **before** the object so it is destroyed after it —
  // see Wire on why that matters even for a symmetric edge.
  struct Rig {
    Log out;
    mOffer obj;

    Rig() {
      Wire(obj, 0, out, 0);
    }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;
    Rig(Rig&&) = delete;
    Rig& operator=(Rig&&) = delete;

    void Int(int value, int inlet = 0) {
      obj.GetInlet(inlet)->SetInt(value, YSE::T_GUI);
    }
    void Float(float value, int inlet = 0) {
      obj.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
    void Bang(int inlet = 0) {
      obj.GetInlet(inlet)->SetBang(YSE::T_GUI);
    }
    // Store a pair the way a patch does: the y first, then the x that spends it.
    void Store(int x, int y) {
      Int(y, 1);
      Int(x, 0);
    }
  };

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("offer: creatable through the registry (#544)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::M_OFFER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".offer");
  }

  TEST_CASE("offer: listed by pRegistry::AllNames (#544)") {
    auto names = Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".offer")) != names.end());
  }

  TEST_CASE("offer: the shape is two inlets and one int outlet (#544)") {
    // Max's shape: an x inlet and a y inlet, and one outlet that only ever
    // carries a y back out.
    mOffer obj;
    CHECK(obj.NumInputs() == 2);
    CHECK(obj.NumOutputs() == 1);
    CHECK(obj.GetOutputType(0) == YSE::OUT_TYPE::INT);
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::MIDI);
    CHECK_FALSE(obj.IsDSPObject());
    CHECK_FALSE(obj.WantsBlockPoll());
  }

  TEST_CASE("offer: documents itself (#544)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(Register().Get(YSE::OBJ::M_OFFER));
    REQUIRE(obj != nullptr);
    CHECK(std::string(obj->Type()) == std::string(YSE::OBJ::M_OFFER));
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MIDI);
    for (int i = 0; i < obj->NumInputs(); i++)
      CHECK_FALSE(obj->GetInlet(i)->GetDocLabel().empty());
    for (int i = 0; i < obj->NumOutputs(); i++)
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
    // Max's offer takes no creation arguments — "Arguments: None" — so there is
    // nothing to document and nothing a save has to carry.
    CHECK(obj->GetParamDocs().empty());
  }

  TEST_CASE("offer: a fresh object holds nothing and has no y armed (#544)") {
    // The default that matters: an untouched object must treat the first x as a
    // query, not as half of a pair, or dropping one into a patch would swallow
    // the first note that reached it.
    Rig rig;
    CHECK(rig.obj.Count() == 0);
    CHECK_FALSE(rig.obj.HasPendingY());
    CHECK(rig.obj.PendingY() == 0);
    CHECK(rig.obj.Dropped() == 0);
  }

  // ─── arming, storing, withdrawing ───────────────────────────────────────────

  TEST_CASE("offer: the right inlet arms a y and sends nothing (#544)") {
    // Max: "the number specifies a y value to be stored in offer. The next x
    // value (int) received in the left inlet causes the two numbers to be stored
    // together as an x,y pair." So it stores nothing by itself.
    Rig rig;
    rig.Int(72, 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.obj.HasPendingY());
    CHECK(rig.obj.PendingY() == 72);
  }

  TEST_CASE("offer: an x with a y armed stores the pair and sends nothing (#544)") {
    Rig rig;
    rig.Store(60, 72);
    CHECK(rig.out.n() == 0);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 72);
    // The y was spent by the x that stored it.
    CHECK_FALSE(rig.obj.HasPendingY());
  }

  TEST_CASE("offer: an x with no y armed withdraws the matching y (#544)") {
    // The other half of Max's int method: "offer looks for an x value that
    // matches the incoming number, sends out the corresponding y value, then
    // deletes the stored pair."
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();

    rig.Int(60);
    CHECK(rig.out.trace() == "72");
    CHECK(rig.obj.Count() == 0); // deleted, which is what "one-time" means
    CHECK_FALSE(rig.obj.Contains(60));
  }

  TEST_CASE("offer: a pair answers exactly once (#544)") {
    // The digest — "store one-time number pairs" — as a test. A second x finds
    // nothing, because the first one took the pair away with it.
    Rig rig;
    rig.Store(60, 72);
    rig.Int(60);
    REQUIRE(rig.out.trace() == "72");
    rig.out.Clear();

    rig.Int(60);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("offer: an x that matches nothing does nothing (#544)") {
    // Max: "if there is no x value stored in offer that matches the number
    // received, offer does nothing." Not a 0, not a bang — nothing.
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();

    rig.Int(61);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 1); // and the pair it did not match is untouched
    CHECK(rig.obj.Contains(60));
  }

  TEST_CASE("offer: arming is one-shot — the next x is a query again (#544)") {
    // The state machine, in one case. This is the object: the same inlet stores
    // and withdraws, and only the armed y decides which.
    Rig rig;
    rig.Store(60, 72);
    CHECK(rig.out.n() == 0);

    rig.Int(60); // no y armed any more, so this is a query
    CHECK(rig.out.trace() == "72");
  }

  TEST_CASE("offer: a second y before any x replaces the first (#544)") {
    // Only one y can be waiting — Max's "the next x value" is singular.
    Rig rig;
    rig.Int(72, 1);
    rig.Int(84, 1);
    CHECK(rig.obj.PendingY() == 84);

    rig.Int(60);
    CHECK(rig.obj.Count() == 1);
    CHECK(rig.obj.YAt(0) == 84);
  }

  TEST_CASE("offer: 0 is a perfectly good y (#544)") {
    // The case an implementation testing `y != 0` for "armed" would fail, and
    // the one that matters most for the note stream Max designed this for: 0 is
    // the velocity a note-off carries and a perfectly ordinary transposed pitch.
    Rig rig;
    rig.Store(60, 0);
    CHECK(rig.out.n() == 0);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.YAt(0) == 0);

    rig.Int(60);
    CHECK(rig.out.trace() == "0"); // a real answer, not a silence
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("offer: 0 is a perfectly good x too (#544)") {
    Rig rig;
    rig.Store(0, 99);
    rig.out.Clear();
    rig.Int(0);
    CHECK(rig.out.trace() == "99");
  }

  TEST_CASE("offer: negative numbers are stored and found like any other (#544)") {
    // Nothing here is range-checked: this is a keyed store, not a note
    // formatter, and a patch keying on an interval rather than a pitch is
    // entitled to its numbers.
    Rig rig;
    rig.Store(-5, -300);
    rig.out.Clear();
    rig.Int(-5);
    CHECK(rig.out.trace() == "-300");
  }

  TEST_CASE("offer: floats are converted to ints on both inlets (#544)") {
    // Max's int inlets take a float by converting it, and this object stores
    // ints and nothing else.
    Rig rig;
    rig.Float(72.9f, 1);
    CHECK(rig.obj.PendingY() == 72);
    rig.Float(60.4f, 0);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 72);

    rig.out.Clear();
    rig.Float(60.7f, 0); // the same x once truncated, so it finds the pair
    CHECK(rig.out.trace() == "72");
  }

  // ─── the list form ──────────────────────────────────────────────────────────

  TEST_CASE("offer: a two-element list stores the pair, '<x> <y>' (#544)") {
    // Max's Discussion: the two numbers "may be sent in the right inlet,
    // immediately followed by the original, or they can be sent as a list".
    // offer has no list method of its own, so a list is Max's inlet
    // distribution, and under right-to-left delivery the second element arms the
    // y before the first is applied as the x.
    Rig rig;
    rig.List("60 72");
    CHECK(rig.out.n() == 0);
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.XAt(0) == 60);
    CHECK(rig.obj.YAt(0) == 72);
    CHECK_FALSE(rig.obj.HasPendingY());

    rig.List("60");
    CHECK(rig.out.trace() == "72");
  }

  TEST_CASE("offer: a one-element list is a bare x (#544)") {
    // Which is what lets a `.m 60` reach this inlet as a query rather than as
    // half a pair.
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();
    rig.List("60");
    CHECK(rig.out.trace() == "72");
  }

  TEST_CASE("offer: elements past the second are ignored (#544)") {
    Rig rig;
    rig.List("60 72 99 100");
    REQUIRE(rig.obj.Count() == 1);
    CHECK(rig.obj.YAt(0) == 72);
    CHECK_FALSE(rig.obj.HasPendingY());
  }

  TEST_CASE("offer: a list on the right inlet arms the y (#544)") {
    Rig rig;
    rig.List("72", 1);
    CHECK(rig.obj.PendingY() == 72);
    rig.Int(60);
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("offer: a non-numeric message does nothing on either inlet (#544)") {
    // Max documents no `anything` method, so a word that is not one of its
    // messages is not one of its messages.
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();

    rig.List("wibble");
    rig.List("wibble", 1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 1);
    CHECK_FALSE(rig.obj.HasPendingY());
  }

  // ─── bang and clear ─────────────────────────────────────────────────────────

  TEST_CASE("offer: a bang sends every stored y, oldest first (#544)") {
    // Max: "bang will cause offer to output every y-value received since the
    // last clear message was received (or since the last initialization)."
    // Oldest first is the order that sentence reads in, and it is deliberately
    // the opposite of `.bag`'s newest-first bang.
    Rig rig;
    rig.Store(60, 72);
    rig.Store(62, 74);
    rig.Store(64, 76);
    rig.out.Clear();

    rig.Bang();
    CHECK(rig.out.trace() == "72 74 76");
  }

  TEST_CASE("offer: a bang deletes nothing (#544)") {
    // The half that separates dumping the store from spending it. Max gives the
    // bang no deletion sentence, and only one of the two gestures is
    // recoverable.
    Rig rig;
    rig.Store(60, 72);
    rig.Store(62, 74);
    rig.Bang();
    rig.out.Clear();

    CHECK(rig.obj.Count() == 2);
    rig.Bang();
    CHECK(rig.out.trace() == "72 74");
    // And the pairs still answer their own x afterwards.
    rig.out.Clear();
    rig.Int(60);
    CHECK(rig.out.trace() == "72");
  }

  TEST_CASE("offer: a bang no longer sends a y that was withdrawn (#544)") {
    // Max's "every y-value received since the last clear" cannot mean the ones
    // already retrieved: the description says "when a pair is retrieved, it is
    // deleted from the collection", and an object holding a second copy of
    // everything ever handed to it would need unbounded memory to contradict its
    // own digest with.
    Rig rig;
    rig.Store(60, 72);
    rig.Store(62, 74);
    rig.Int(60);
    rig.out.Clear();

    rig.Bang();
    CHECK(rig.out.trace() == "74");
  }

  TEST_CASE("offer: a bang on an empty store sends nothing (#544)") {
    Rig rig;
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("offer: a bang does not spend an armed y (#544)") {
    // A bang is not an x, so it cannot complete a pair.
    Rig rig;
    rig.Int(72, 1);
    rig.Bang();
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.HasPendingY());
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("offer: a bang on the right inlet does nothing (#544)") {
    // Max gives the right inlet only an int method.
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();
    rig.Bang(1);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("offer: clear empties the store silently (#544)") {
    // Max: "deletes the entire contents of offer".
    Rig rig;
    rig.Store(60, 72);
    rig.Store(62, 74);
    rig.out.Clear();

    rig.List("clear");
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 0);
    rig.Bang();
    CHECK(rig.out.n() == 0);
  }

  TEST_CASE("offer: clear disarms a waiting y (#544)") {
    // Not in Max's sentence and it follows from it: a `clear` is "forget
    // everything you were told", and a y carried across it would make the first
    // x after the clear disappear into a pair the patch had already written off.
    Rig rig;
    rig.Int(72, 1);
    REQUIRE(rig.obj.HasPendingY());

    rig.List("clear");
    CHECK_FALSE(rig.obj.HasPendingY());

    // So the next x is a query, and it finds nothing.
    rig.Int(60);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("offer: 'clear' on the right inlet is not a command (#544)") {
    // Max scopes clear to the left inlet, and the right one takes numbers only.
    Rig rig;
    rig.Store(60, 72);
    rig.List("clear", 1);
    CHECK(rig.obj.Count() == 1);
    CHECK_FALSE(rig.obj.HasPendingY());
  }

  // ─── duplicates, and which pair a query takes ───────────────────────────────

  TEST_CASE("offer: the same x stored twice keeps both pairs (#544)") {
    // Max leaves this open and it is the case that matters most: the same key
    // struck twice before the first note-off, which every sustained passage
    // produces. Overwriting would lose a note the synth was still sounding.
    Rig rig;
    rig.Store(60, 72);
    rig.Store(60, 67);
    CHECK(rig.obj.Count() == 2);
    CHECK(rig.obj.CountOf(60) == 2);
  }

  TEST_CASE("offer: a query takes the newest matching pair (#544)") {
    // `.bag`'s rule and for `.bag`'s reason: a store and a withdrawal are then
    // an exact undo of each other, so a re-struck key unwinds in the order the
    // keys came down.
    Rig rig;
    rig.Store(60, 72);
    rig.Store(60, 67);
    rig.out.Clear();

    rig.Int(60);
    CHECK(rig.out.trace() == "67");
    rig.Int(60);
    CHECK(rig.out.trace() == "67 72");
    CHECK(rig.obj.Count() == 0);
  }

  TEST_CASE("offer: withdrawing from the middle keeps the rest in order (#544)") {
    // The gap has to close, or a bang would start reporting entries the store
    // no longer holds.
    Rig rig;
    rig.Store(60, 1);
    rig.Store(62, 2);
    rig.Store(64, 3);
    rig.Int(62);
    rig.out.Clear();

    CHECK(rig.obj.Count() == 2);
    rig.Bang();
    CHECK(rig.out.trace() == "1 3");
  }

  // ─── the capacity ───────────────────────────────────────────────────────────

  TEST_CASE("offer: a store past the capacity is refused, and spends the y (#544)") {
    // The table is fixed and allocated with the object, because growing it would
    // allocate on whichever thread the message arrived on. The refused pair
    // still spends the armed y: keeping it would pair it with the *next* x
    // instead, which is a wrong answer where dropping it is merely a missing
    // one.
    Rig rig;
    for (std::size_t i = 0; i < mOffer::MAX_PAIRS; i++)
      rig.Store((int)i, (int)i + 1000);
    REQUIRE(rig.obj.Count() == mOffer::MAX_PAIRS);

    rig.out.Clear();
    rig.Store(9999, 8888);
    CHECK(rig.obj.Count() == mOffer::MAX_PAIRS);
    CHECK_FALSE(rig.obj.Contains(9999));
    CHECK_FALSE(rig.obj.HasPendingY());
    CHECK(rig.out.n() == 0);

    // Everything already stored still answers.
    rig.Int(0);
    CHECK(rig.out.trace() == "1000");
  }

  // ─── real-time discipline ───────────────────────────────────────────────────

  TEST_CASE("offer: Calculate sends nothing (#544)") {
    // The object is driven entirely by its inlets; one that emitted would
    // withdraw a pair on every DSP tick from a stimulus no patch sent.
    Rig rig;
    rig.Store(60, 72);
    rig.out.Clear();
    for (int i = 0; i < 8; i++)
      rig.obj.Calculate(YSE::T_DSP);
    CHECK(rig.out.n() == 0);
    CHECK(rig.obj.Count() == 1);
  }

  TEST_CASE("offer: an outlet wired back into the inlet is answered and terminates (#544)") {
    // The guard is released before the send, so a patch that loops the outlet
    // back into inlet 0 is *answered* rather than refused — and it cannot run
    // away, because every re-entrant query deletes the pair it answered. Here
    // 60 finds 62, 62 finds 64 and 64 finds nothing, so the chain unwinds itself
    // and the store empties.
    Rig rig;
    rig.Store(60, 62);
    rig.Store(62, 64);
    Wire(rig.obj, 0, rig.obj, 0);
    rig.out.Clear();

    rig.Int(60);
    CHECK(rig.obj.Count() == 0);
    CHECK(rig.out.n() == 2);
    CHECK(std::find(rig.out.values.begin(), rig.out.values.end(), 62) != rig.out.values.end());
    CHECK(std::find(rig.out.values.begin(), rig.out.values.end(), 64) != rig.out.values.end());
  }

  TEST_CASE("offer: a bang burst is captured before the first send (#544)") {
    // `.bucket`'s and `.bag`'s stack array. A re-entrant message that empties
    // the store mid-burst must not shorten the burst already on its way out, or
    // a patch looping the outlet back would see a dump that changed under it.
    Rig rig;
    rig.Store(60, 1);
    rig.Store(62, 2);
    rig.Store(64, 3);
    Wire(rig.obj, 0, rig.obj, 0); // every y sent comes straight back as an x
    rig.out.Clear();

    rig.Bang();
    // The three captured values all went out, whatever the re-entrant queries
    // did to the store behind them.
    CHECK(rig.out.n() >= 3);
    CHECK(rig.out.values[0] == 1);
  }

  TEST_CASE("offer: the message paths allocate nothing (#544)") {
    // Everything a message can do, measured on the thread that does it. A note
    // routinely arrives on the audio callback — an in-patcher dispatch runs on
    // T_DSP — so every one of these paths is audio-thread code, the bang dump
    // included.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig;
    // A store with something in it, so the walks below have work to do.
    rig.Store(60, 72);
    rig.Store(62, 74);

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string pair = "64 90";
    const std::string one = "62";
    const std::string many = "66 91 92 93";
    const std::string word = "wibble";
    const std::string clear = "clear";
    const std::string y = "77";
    {
      TestHelpers::ProbeScope probe;
      rig.Int(90, 1);
      rig.Float(90.5f, 1);
      rig.List(y, 1);
      rig.List(word, 1);
      rig.Int(70); // a store
      rig.Int(70); // and the withdrawal that empties it again
      rig.Float(60.2f); // a withdrawal of a pair stored before the probe
      rig.List(pair); // a store through the list form
      rig.List(one); // a query through the list form
      rig.List(many);
      rig.List(word);
      rig.Bang(); // the dump, walking the whole store
      rig.Bang(1); // and the inlet that has no bang method
      rig.List(clear);
      rig.Bang();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // The probed messages really did something — an assertion that only proves
    // nothing happened proves nothing.
    CHECK(rig.out.n() > 0);
  }

  // ─── what a save carries ────────────────────────────────────────────────────

  TEST_CASE("offer: it survives a DumpJSON / ParseJSON round trip (#544)") {
    // There are no creation arguments to carry — Max's offer takes none — so
    // what a save has to preserve is the object itself, under its own name. The
    // contents deliberately do not survive: a reloaded patch holding the
    // mappings for notes that were sounding when it was saved would be holding
    // answers to note-offs that are never coming.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::M_OFFER, "");
    REQUIRE(obj != nullptr);
    obj->SetIntData(1, 72);
    obj->SetIntData(0, 60);

    const std::string json = src.DumpJSON();
    CHECK(json.find(".offer") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* copy = loaded.GetHandleFromList(0);
    REQUIRE(copy != nullptr);
    CHECK(std::string(copy->Type()) == ".offer");
    CHECK(std::string(copy->GetParams()).empty());
  }

  // ─── end to end, in a real patcher ──────────────────────────────────────────

  TEST_CASE("offer: a real transposer chain answers the note-off (#544)") {
    // Max's own use case, assembled out of real objects and driven by real MIDI
    // bytes: "offer was designed for use with algorithms that transform the
    // pitch of an incoming note stream. By storing the original and transformed
    // note together ... the transformed pitch can be retrieved when the note-off
    // is received."
    //
    // A real `.midiparse` decodes the bytes, a real `.unpack` splits the pair, a
    // real `.trigger` orders the two branches right to left, a real `.stripnote`
    // lets only note-ons reach the transposer, and a real `.+` computes the
    // pitch the synth will actually sound and arms it as the y. The x is the
    // pitch the *player* touched, and it arrives second — so a note-on stores
    // and a note-off, whose velocity `.stripnote` swallowed, finds nothing armed
    // and withdraws instead. The whole store/withdraw split is carried by the
    // patch's shape, which is exactly what Max's object is for.
    Log out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* unpack = p.CreateObject(YSE::OBJ::G_UNPACK, "0 0");
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "i i");
    YSE::pHandle* strip = p.CreateObject(YSE::OBJ::M_STRIPNOTE, "");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "7");
    YSE::pHandle* offer = p.CreateObject(YSE::OBJ::M_OFFER, "");
    REQUIRE(parse != nullptr);
    REQUIRE(unpack != nullptr);
    REQUIRE(trig != nullptr);
    REQUIRE(strip != nullptr);
    REQUIRE(add != nullptr);
    REQUIRE(offer != nullptr);

    p.Connect(parse, 0, unpack, 0); // the note pair, on one cord
    p.Connect(unpack, 1, strip, 1); // velocity, cold — it fires first
    p.Connect(unpack, 0, trig, 0); // pitch, hot
    p.Connect(trig, 1, strip, 0); // right outlet first: note-ons only
    p.Connect(strip, 0, add, 0); // ... transposed ...
    p.Connect(add, 0, offer, 1); // ... and armed as the y
    p.Connect(trig, 0, offer, 0); // left outlet second: the x
    p.Connect(offer, 0, &outHandle, 0);

    const int noteOn[] = {0x90, 60, 100};
    for (int b : noteOn)
      parse->SetIntData(0, b);
    // A note-on stores and says nothing. The synth is being told 67 by the
    // transposer's own branch; this object is only keeping the receipt.
    CHECK(out.n() == 0);

    const int noteOff[] = {0x90, 60, 0};
    for (int b : noteOff)
      parse->SetIntData(0, b);
    // The note-off carried 60 — the key the player released — and what comes
    // back is 67, the pitch that is actually sounding.
    CHECK(out.trace() == "67");

    // And the receipt is gone, so a second note-off for the same key answers
    // nothing rather than releasing a note twice.
    out.Clear();
    for (int b : noteOff)
      parse->SetIntData(0, b);
    CHECK(out.n() == 0);
  }

  TEST_CASE("offer: the same chain keeps each note's own transposition (#544)") {
    // The reason the mapping has to be *stored* rather than recomputed, and the
    // bug the object exists to prevent: the transposition changes while a note
    // is still held. Recomputing at note-off time would release a pitch nothing
    // is sounding and leave the real one hanging forever.
    Log out;
    YSE::pHandle outHandle(&out);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* parse = p.CreateObject(YSE::OBJ::M_PARSE, "");
    YSE::pHandle* unpack = p.CreateObject(YSE::OBJ::G_UNPACK, "0 0");
    YSE::pHandle* trig = p.CreateObject(YSE::OBJ::G_TRIGGER, "i i");
    YSE::pHandle* strip = p.CreateObject(YSE::OBJ::M_STRIPNOTE, "");
    YSE::pHandle* add = p.CreateObject(YSE::OBJ::G_ADD, "7");
    YSE::pHandle* offer = p.CreateObject(YSE::OBJ::M_OFFER, "");
    REQUIRE(offer != nullptr);
    REQUIRE(add != nullptr);

    p.Connect(parse, 0, unpack, 0);
    p.Connect(unpack, 1, strip, 1);
    p.Connect(unpack, 0, trig, 0);
    p.Connect(trig, 1, strip, 0);
    p.Connect(strip, 0, add, 0);
    p.Connect(add, 0, offer, 1);
    p.Connect(trig, 0, offer, 0);
    p.Connect(offer, 0, &outHandle, 0);

    const int on60[] = {0x90, 60, 100};
    for (int b : on60)
      parse->SetIntData(0, b); // 60 stored against 67

    add->SetIntData(1, 5); // the transposer is turned down mid-phrase

    const int on64[] = {0x90, 64, 100};
    for (int b : on64)
      parse->SetIntData(0, b); // 64 stored against 69

    REQUIRE(out.n() == 0);

    const int off60[] = {0x90, 60, 0};
    for (int b : off60)
      parse->SetIntData(0, b);
    const int off64[] = {0x90, 64, 0};
    for (int b : off64)
      parse->SetIntData(0, b);

    // 67 for the first note even though the transposer now says +5, and 69 for
    // the second. Two keys, two receipts, neither one recomputed.
    CHECK(out.trace() == "67 69");
  }
}

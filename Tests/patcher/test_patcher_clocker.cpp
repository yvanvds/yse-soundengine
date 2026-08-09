// Tests for `.clocker` — Max's clocker, "report elapsed time, at regular
// intervals" (issues #505 and #725).
//
// Two suites, because the object has two engines and they need different
// worlds. The `patcher` suite below covers the millisecond clocker of #505 and
// the *grammar* of #725 — what a note value, a tick count and a `clock`
// message do to an object with no clock to bind. The `clock` suite at the
// bottom covers #725's beat engine end to end against a real
// `CLOCK::domainClock` stepped deterministically from the test thread, which is
// what that suite is isolated for.
//
// The millisecond half splits cleanly into three claims, and the cases are
// grouped by which one they carry:
//
//   - **grammar** cases drive a standalone object with an interval long enough
//     that the real timer never fires, so what they observe is entirely the
//     message handlers: Max's four left-inlet methods (int, float, bang,
//     `stop`), his `reset`, and the interval on the right inlet. A bang and a
//     non-zero int are deliberately *not* the same method here — Max's page
//     gives bang a running-object rule of its own — and that difference is
//     pinned rather than assumed.
//
//   - **measurement** cases are the heart of #505: the elapsed time is read
//     off the engine's monotonic clock, never counted from ticks. They drive
//     `Tick()` directly at controlled instants, which is what makes the claim
//     falsifiable — an implementation that multiplied a tick count by the
//     interval would report the interval, and these cases run at an interval
//     of 100 seconds.
//
//   - **run** cases let the real `timerThread` deliver, so they cover what a
//     direct call structurally cannot: that the object actually reports at its
//     interval, that a stop is immediate, that a clocker stopped from inside
//     its own tick does not wedge the one timer worker in the process, and
//     that destroying a running one takes its timer with it.
//
// Plus the usual per-object obligations: the registry entry, an allocation
// probe over the message paths, a DumpJSON / ParseJSON round trip, and an
// integration case that starts the object from inside `Calculate` — the audio
// callback — through a real `.delay` -> `.clocker` patch.
//
// A running clocker sends from the `timerThread` worker, so every sink that
// may be hit by one guards its state; the grammar and measurement sinks are
// only ever touched from the test thread. No audio device required.

#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "clock/clockManager.h"
#include "patcher/inlet.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/time/TimerThread.h"
#include "patcher/time/clockBridge.h"
#include "patcher/time/gClocker.h"
#include "support/alloc_probe.hpp"

using namespace std::chrono_literals;

namespace {

  using YSE::PATCHER::gClocker;
  using YSE::PATCHER::patcherImplementation;

  // Long enough that the real timer cannot fire inside a test, so a case that
  // wants to control *when* a report happens can call `Tick()` itself.
  constexpr int kNeverFires = 100000;

  // Records every number this object ever sent, in order. That sequence is the
  // whole observable surface of a `.clocker`: one outlet, one kind of message,
  // and the values are the point.
  struct Recorder : YSE::PATCHER::pObject {
    std::vector<int> values;

    Recorder() : pObject(false) {
      // Reserved up front so the allocation probe measures the *object* rather
      // than this sink's own vector growing under it.
      values.reserve(1024);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) { values.push_back(v); });
    }
    const char* Type() const override {
      return "clocker_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
  };

  // The same, for a *running* clocker: the sends arrive on the timerThread
  // worker while the test thread reads them.
  struct SharedRecorder : YSE::PATCHER::pObject {
    mutable std::mutex mtx;
    std::vector<int> values;
    std::atomic<int> count{0};

    SharedRecorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
        {
          std::scoped_lock lock(mtx);
          values.push_back(v);
        }
        count.fetch_add(1, std::memory_order_release);
      });
    }
    const char* Type() const override {
      return "clocker_shared_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    int n() const {
      return count.load(std::memory_order_acquire);
    }
    std::vector<int> snapshot() const {
      std::scoped_lock lock(mtx);
      return values;
    }
    int last() const {
      std::scoped_lock lock(mtx);
      return values.empty() ? -1 : values.back();
    }
  };

  // Switches the clocker feeding it off from inside the very tick that
  // delivered the number — an object in the patch stopping the clocker. That
  // handler runs on the timer worker, inside `gClocker::Tick`, which is the
  // case the object has to route around the blocking half of `timerBridge`.
  struct SelfStoppingSink : YSE::PATCHER::pObject {
    std::atomic<int> count{0};
    gClocker* target = nullptr;
    int stopAt = 1;

    SelfStoppingSink() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterInt([this](int, int, YSE::THREAD thread) {
        const int n = count.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (target != nullptr && n == stopAt) target->GetInlet(0)->SetInt(0, thread);
      });
    }
    const char* Type() const override {
      return "clocker_self_stopping_sink";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    int n() const {
      return count.load(std::memory_order_acquire);
    }
  };

  // A standalone `.clocker` with a recorder on its outlet. Standalone means no
  // patcher, which is what makes this rig the right place for everything that
  // is not about the patcher's own threads.
  struct Rig {
    gClocker obj;
    Recorder out;

    explicit Rig(const std::string& args = "") {
      if (!args.empty()) obj.SetParams(args);
      obj.ConnectOutlet(out.GetInlet(0), 0);
    }
    ~Rig() {
      // Stopped before destruction, the way a patch's teardown would: the
      // lifetime case below is the one that deliberately does not.
      obj.GetInlet(0)->SetInt(0, YSE::T_GUI);
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
    void Bang() {
      obj.GetInlet(0)->SetBang(YSE::T_GUI);
    }
    void List(const std::string& message, int inlet = 0) {
      obj.GetInlet(inlet)->SetList(message, YSE::T_GUI);
    }
  };

  // Poll `pred` every millisecond up to `budgetMs`, so a case finishes as soon
  // as the timer delivers rather than sleeping a fixed worst case.
  template <typename P> bool waitFor(P pred, int budgetMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline) {
      if (pred()) return true;
      std::this_thread::sleep_for(1ms);
    }
    return pred();
  }

} // namespace

TEST_SUITE("patcher") {

  // ─── the object exists ──────────────────────────────────────────────────────

  TEST_CASE("clocker: the object is registered and creatable (#505)") {
    bool found = false;
    for (const std::string& name : YSE::PATCHER::Register().AllNames()) {
      if (name == YSE::OBJ::G_CLOCKER) found = true;
    }
    CHECK(found);

    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_CLOCKER);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == ".clocker");
    CHECK(h->GetInputs() == 2);
    // Two outlets since #725: the milliseconds Max's object has always sent,
    // and the beats of a bound domain clock. Two outlets rather than one outlet
    // whose unit depends on whether a clock is bound, so that binding one never
    // changes what a patch downstream is reading — `.timer`'s answer (#506) to
    // the same question.
    CHECK(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::INT);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::FLOAT);
  }

  TEST_CASE("clocker: the interval defaults to Max's 5 ms and takes the creation argument (#505)") {
    // Max: "if there is no argument, the initial time interval is set to 5
    // milliseconds."
    Rig fresh;
    CHECK(fresh.obj.IntervalMs() == 5);

    Rig given("250");
    CHECK(given.obj.IntervalMs() == 250);
    CHECK(given.obj.GetParams() == "250");
  }

  TEST_CASE("clocker: it is created stopped and reads 0 (#505)") {
    Rig rig(std::to_string(kNeverFires));
    CHECK_FALSE(rig.obj.Running());
    // Max: a stopped clocker "always resets to 0 anyway".
    CHECK(rig.obj.Elapsed() == 0);
    CHECK(rig.out.values.empty());
  }

  // ─── grammar: Max's left-inlet methods ──────────────────────────────────────

  TEST_CASE("clocker: a non-zero int starts it and 0 stops it (#505)") {
    // Max: "any non-zero number starts the clocker object. ... 0 stops the
    // clocker object."
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    CHECK(rig.obj.Running());
    // Nothing is emitted on the start itself: Max documents an immediate output
    // for `metro` and documents none here, and 0 is what a stopped clocker
    // already reads.
    CHECK(rig.out.values.empty());

    rig.Int(0);
    CHECK_FALSE(rig.obj.Running());
    CHECK(rig.obj.Elapsed() == 0);

    rig.Int(-7); // "any non-zero number" includes a negative one
    CHECK(rig.obj.Running());
  }

  TEST_CASE("clocker: a float starts and stops uncast (#505)") {
    // Max: "float — same as int", and int's rule is "any non-zero number
    // starts". 0.5 is a number other than 0, so it starts; a clocker that cast
    // its float to int would stop instead.
    Rig rig(std::to_string(kNeverFires));

    rig.Float(0.5f);
    CHECK(rig.obj.Running());
    rig.Float(0.f);
    CHECK_FALSE(rig.obj.Running());
    rig.Float(-2.5f);
    CHECK(rig.obj.Running());
  }

  TEST_CASE("clocker: 'stop' in the left inlet stops it, and elsewhere is not a command (#505)") {
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    rig.List("stop");
    CHECK_FALSE(rig.obj.Running());

    // Max documents stop as a left-inlet method. On the interval inlet the word
    // is only a token that is not a number, and does nothing at all — neither
    // stopping the clocker nor disturbing the interval.
    rig.Int(1);
    rig.List("stop", 1);
    CHECK(rig.obj.Running());
    CHECK(rig.obj.IntervalMs() == kNeverFires);
  }

  TEST_CASE("clocker: a bang starts a stopped clocker (#505)") {
    // Max: "if the clocker object is not running, a bang message will start the
    // count."
    Rig rig(std::to_string(kNeverFires));

    rig.Bang();
    CHECK(rig.obj.Running());
    CHECK(rig.out.values.empty());
  }

  TEST_CASE(
      "clocker: a bang into a running clocker resets the count, and does not stop it (#505)") {
    // Max: "if the clocker object is running, a bang message will reset the
    // count." This is the one place `.clocker` and `.metro` part company on the
    // same word — a `.metro` re-starts, and Max's clocker page gives bang a
    // rule of its own.
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    std::this_thread::sleep_for(40ms);
    REQUIRE(rig.obj.Elapsed() >= 20);

    rig.Bang();
    CHECK(rig.obj.Running()); // not a toggle: a bang never stops one
    CHECK(rig.obj.Elapsed() < 20); // the baseline moved to now
  }

  TEST_CASE("clocker: 'reset' zeroes a running clocker and is a no-op on a stopped one (#505)") {
    // Max: "resets the elapsed time to 0 without stopping or restarting the
    // clock", and "this message is meaningless when the clocker is not running,
    // since it always resets to 0 anyway when stopped."
    Rig rig(std::to_string(kNeverFires));

    rig.List("reset");
    CHECK_FALSE(rig.obj.Running()); // a reset never starts anything
    CHECK(rig.obj.Elapsed() == 0);

    rig.Int(1);
    std::this_thread::sleep_for(40ms);
    REQUIRE(rig.obj.Elapsed() >= 20);

    rig.List("reset");
    CHECK(rig.obj.Running());
    CHECK(rig.obj.Elapsed() < 20);
  }

  TEST_CASE("clocker: an unknown word does nothing (#505)") {
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    rig.List("quantize 4n");
    rig.List("autostart 1");
    rig.List("1.1.0"); // bars.beats.units: needs a meter a domain clock has not
    CHECK(rig.obj.Running());
    CHECK(rig.obj.IntervalMs() == kNeverFires);
    CHECK(rig.out.values.empty());
  }

  TEST_CASE("clocker: 'clock' with no patcher binds nothing and is not a start (#725)") {
    // A standalone object has no patcher and therefore no bridge to bind
    // through. The word must still be read as the command it is: not as a
    // number, not as a start, and not as an interval.
    Rig rig(std::to_string(kNeverFires));

    rig.List("clock main");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK(std::string(rig.obj.ClockName()).empty());
    CHECK_FALSE(rig.obj.Running());
    CHECK(rig.obj.IntervalMs() == kNeverFires);

    // And a bare `clock` — Max's "sets the object back to using Max's regular
    // millisecond clock" — is inert on an object that was never bound.
    rig.List("clock");
    CHECK_FALSE(rig.obj.OnClock());
    CHECK_FALSE(rig.obj.Running());
    CHECK(rig.out.values.empty());
  }

  // ─── grammar: the interval ──────────────────────────────────────────────────

  TEST_CASE("clocker: the right inlet sets the interval as int, float or list (#505)") {
    Rig rig(std::to_string(kNeverFires));

    rig.Int(400, 1);
    CHECK(rig.obj.IntervalMs() == 400);
    rig.Float(120.7f, 1);
    CHECK(rig.obj.IntervalMs() == 120);
    rig.List("60", 1);
    CHECK(rig.obj.IntervalMs() == 60);

    // Floored at the 1 ms the timer can keep, rather than degenerating into a
    // one-shot or a spin.
    rig.Int(0, 1);
    CHECK(rig.obj.IntervalMs() == 1);
    rig.Int(-5, 1);
    CHECK(rig.obj.IntervalMs() == 1);
  }

  TEST_CASE("clocker: a note value or a tick count sets the interval in beats (#725)") {
    // #505 refused every tempo-relative spelling, having no clock to read one
    // against; #725 reads them through the shared `timeValue.h`, so this object
    // and `.metro` cannot drift apart on one syntax. A quarter note is one beat
    // on a domain clock, and Max's tick is 1/480th of one.
    Rig rig("250");
    CHECK(rig.obj.IntervalBeats() == 0.0); // milliseconds until told otherwise

    rig.List("4n", 1);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(1.0));
    rig.List("4nd", 1);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(1.5));
    rig.List("8nt", 1);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(1.0 / 3.0));
    rig.List("1440 ticks", 1);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(3.0));
    // The millisecond interval is untouched underneath, waiting for a plain
    // number to select it again.
    CHECK(rig.obj.IntervalMs() == 250);

    // Max's "the number is the time interval, in milliseconds" is a statement
    // about the *unit*, so any plain number puts the object back on
    // milliseconds — `.metro`'s rule for the same message.
    rig.List("60", 1);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    CHECK(rig.obj.IntervalMs() == 60);
    rig.List("4n", 1);
    rig.Int(80, 1);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    rig.List("4n", 1);
    rig.Float(90.5f, 1);
    CHECK(rig.obj.IntervalBeats() == 0.0);
    CHECK(rig.obj.IntervalMs() == 90);
  }

  TEST_CASE("clocker: '1440 ticks' is not read as 1440 milliseconds (#725)") {
    // The trap #505 documented and answered by refusing the whole family: Max's
    // tick spelling begins with a digit, so a reader that took the leading
    // number would set an interval nobody asked for, wrong by whatever the
    // tempo is and looking like it worked. #725 answers it by reading the
    // tempo-relative form *first*.
    Rig rig("250");

    rig.List("1440 ticks", 1);
    CHECK(rig.obj.IntervalMs() == 250);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(3.0));

    // bars.beats.units still needs a meter a bare beat accumulator has not, so
    // it moves neither unit rather than becoming a 1.
    rig.List("1.2.0", 1);
    CHECK(rig.obj.IntervalMs() == 250);
    CHECK(rig.obj.IntervalBeats() == doctest::Approx(3.0));
  }

  TEST_CASE("clocker: a tempo-relative interval with no clock does not run (#725)") {
    // `.metro`'s rule (#705): falling back to the millisecond interval would be
    // a report on a grid nobody asked for. A standalone object can never bind,
    // so it can never run on beats.
    Rig rig(std::to_string(kNeverFires));

    rig.List("4n", 1);
    rig.Int(1);
    CHECK_FALSE(rig.obj.Running());
    CHECK_FALSE(rig.obj.RunningOnClock());
    rig.Bang();
    CHECK_FALSE(rig.obj.Running());
    rig.obj.Tick();
    CHECK(rig.out.values.empty());

    // And a plain number puts it back on the millisecond engine, where it runs.
    rig.List("500", 1);
    rig.Int(1);
    CHECK(rig.obj.Running());
    CHECK_FALSE(rig.obj.RunningOnClock());
  }

  // ─── measurement: the elapsed time is read, never counted ───────────────────

  TEST_CASE("clocker: the report is the measured elapsed time, not the interval (#505)") {
    // The claim #505 is made of, and the case that falsifies the obvious
    // implementation. The interval here is 100 seconds, so a report built by
    // multiplying a tick count by the interval could only ever say 100000 (or,
    // counting from one, some multiple of it). What comes out is how long the
    // run has actually been going.
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    REQUIRE(rig.out.values.empty());

    std::this_thread::sleep_for(40ms);
    rig.obj.Tick();

    REQUIRE(rig.out.values.size() == 1);
    CHECK(rig.out.values[0] >= 30);
    // Generous: a loaded CI runner can oversleep by a lot, but not by seconds,
    // and the failure this bounds is a report of the *interval*.
    CHECK(rig.out.values[0] < 5000);
  }

  TEST_CASE("clocker: the elapsed time keeps rising across reports (#505)") {
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    std::this_thread::sleep_for(20ms);
    rig.obj.Tick();
    std::this_thread::sleep_for(20ms);
    rig.obj.Tick();
    std::this_thread::sleep_for(20ms);
    rig.obj.Tick();

    REQUIRE(rig.out.values.size() == 3);
    CHECK(rig.out.values[0] < rig.out.values[1]);
    CHECK(rig.out.values[1] < rig.out.values[2]);
    CHECK(rig.out.values[2] >= 45);
  }

  TEST_CASE("clocker: a stopped clocker reports nothing (#505)") {
    Rig rig(std::to_string(kNeverFires));

    // A tick that beat a stop to the punch — the wait-free stop routes allow
    // exactly this — must not emit: `Tick()` reads `running` before it sends.
    rig.obj.Tick();
    CHECK(rig.out.values.empty());

    rig.Int(1);
    rig.Int(0);
    rig.obj.Tick();
    CHECK(rig.out.values.empty());
  }

  TEST_CASE("clocker: a non-zero int re-starts a running clocker from zero (#505)") {
    // Max gives `int` no running-object rule, so "starts the clocker object" is
    // read as `.metro`'s re-start: a fresh baseline, and the tick re-phased
    // with it.
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    std::this_thread::sleep_for(40ms);
    rig.obj.Tick();
    REQUIRE(rig.out.values.size() == 1);
    REQUIRE(rig.out.values[0] >= 30);

    rig.Int(1);
    rig.obj.Tick();
    REQUIRE(rig.out.values.size() == 2);
    CHECK(rig.out.values[1] < 20);
  }

  TEST_CASE("clocker: changing the interval does not move the elapsed time (#505)") {
    // #625's keep-the-phase rule for `.metro`'s millisecond path, and it
    // matters more here: the number is measured from the start of the run, so a
    // re-phase would show up as a jump in what the user reads. The interval
    // moves; the baseline does not.
    Rig rig(std::to_string(kNeverFires));

    rig.Int(1);
    std::this_thread::sleep_for(40ms);
    rig.Int(kNeverFires / 2, 1);
    CHECK(rig.obj.IntervalMs() == kNeverFires / 2);

    rig.obj.Tick();
    REQUIRE(rig.out.values.size() == 1);
    CHECK(rig.out.values[0] >= 30); // and not back at 0
  }

  // ─── run: the real timer delivers ───────────────────────────────────────────

  TEST_CASE("clocker: a running clocker reports at its interval (#505)") {
    // End to end on the real `timerThread`: the object is started and left
    // alone, and what arrives is a rising sequence of elapsed times.
    gClocker clocker;
    SharedRecorder out;
    clocker.SetParams("10");
    clocker.ConnectOutlet(out.GetInlet(0), 0);

    clocker.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(waitFor([&] { return out.n() >= 4; }));

    const std::vector<int> values = out.snapshot();
    for (std::size_t i = 1; i < values.size(); i++)
      CHECK(values[i] >= values[i - 1]);
    // Four reports at 10 ms is 40 ms of run; allow for the first landing early
    // by a rounding of the millisecond.
    CHECK(values.back() >= 20);

    clocker.GetInlet(0)->SetInt(0, YSE::T_GUI);
  }

  TEST_CASE("clocker: a stop ends the reports (#505)") {
    gClocker clocker;
    SharedRecorder out;
    clocker.SetParams("5");
    clocker.ConnectOutlet(out.GetInlet(0), 0);

    clocker.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(waitFor([&] { return out.n() >= 3; }));

    clocker.GetInlet(0)->SetList("stop", YSE::T_GUI);
    const int afterStop = out.n();
    // A 5 ms timer still running would advance many times over this window.
    std::this_thread::sleep_for(100ms);
    CHECK(out.n() == afterStop);
  }

  TEST_CASE(
      "clocker: a reset under a running clocker keeps the tick and rewinds the value (#505)") {
    // Max's "without stopping or restarting the clock; clocker continues to
    // report the new elapsed time at the same regular interval": the reports
    // keep coming, and the number they carry starts again from 0.
    gClocker clocker;
    SharedRecorder out;
    clocker.SetParams("5");
    clocker.ConnectOutlet(out.GetInlet(0), 0);

    clocker.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(waitFor([&] { return out.last() >= 40; }));

    clocker.GetInlet(0)->SetList("reset", YSE::T_GUI);
    // Reports do keep arriving — the reset does not stop the clock.
    const int afterReset = out.n();
    REQUIRE(waitFor([&] { return out.n() > afterReset + 2; }));
    CHECK(clocker.Running());

    clocker.GetInlet(0)->SetInt(0, YSE::T_GUI);

    // And the sequence *fell* somewhere, which is the whole claim and the one
    // assertion that does not depend on how promptly this thread got to look:
    // a clocker that ignored the reset would report a rising sequence forever.
    const std::vector<int> values = out.snapshot();
    bool rewound = false;
    for (std::size_t i = 1; i < values.size(); i++) {
      if (values[i] < values[i - 1]) rewound = true;
    }
    CHECK(rewound);
  }

  TEST_CASE("concurrency: clocker stopped from inside its own tick stops cleanly (#505)") {
    // The cycle a patch can draw with one cord: this outlet back into this left
    // inlet. The handler then runs on the timer worker *inside* `Tick()`, and a
    // stop taken there through the blocking half of `timerBridge` would take a
    // mutex a background reconcile may be holding while it waits for this very
    // callback to return. Neither side could move; the one timer worker in the
    // process would be gone, and with it every other `.metro` and `.clocker`.
    //
    // The `concurrency:` prefix is the selector the sanitizer CI legs run.
    gClocker clocker;
    SelfStoppingSink sink;
    sink.target = &clocker;
    sink.stopAt = 2;

    clocker.SetParams("5");
    clocker.ConnectOutlet(sink.GetInlet(0), 0);

    clocker.GetInlet(0)->SetInt(1, YSE::T_GUI);
    REQUIRE(waitFor([&] { return !clocker.Running(); }));

    // The wait-free stop lands a pool hop later, so a callback already under
    // way may still run — but it finds `running` false and sends nothing, which
    // is the promise that matters. Two reports, and no more.
    const int settled = sink.n();
    CHECK(settled == 2);
    std::this_thread::sleep_for(100ms);
    CHECK(sink.n() == settled);
  }

  TEST_CASE("concurrency: clocker destroyed while running stops its timer (#505)") {
    // The lifetime case `.metro` learned the hard way in #663: an object freed
    // with its timer still armed leaves the worker calling back into freed
    // memory. `~gClocker` gives its bridge slot back, and `timerBridge::Release`
    // keeps `ClearTimer`'s blocking handshake for exactly this caller.
    //
    // The sink is declared first so it outlives the clocker: nothing the timer
    // might still deliver can land on a dead sink and confuse the diagnosis.
    SharedRecorder out;
    const std::size_t timersBefore = YSE::PATCHER::TimerThread().size();

    {
      // Heap-allocated so a sanitizer sees a real free rather than a stack
      // frame that happens to be reused.
      auto clocker = std::make_unique<gClocker>();
      clocker->SetParams("5");
      clocker->ConnectOutlet(out.GetInlet(0), 0);
      clocker->GetInlet(0)->SetInt(1, YSE::T_GUI);
      REQUIRE(waitFor([&] { return out.n() >= 2; }));
      // Destroyed *running*, deliberately.
    }

    CHECK(YSE::PATCHER::TimerThread().size() == timersBefore);
    const int settled = out.n();
    std::this_thread::sleep_for(100ms);
    CHECK(out.n() == settled);
  }

  // ─── real-time behaviour ────────────────────────────────────────────────────

  TEST_CASE("clocker: its message paths allocate nothing (#505)") {
    // A patcher message handler runs on whichever thread dispatched it, and
    // there is no predicate an object can ask up front — so every command word
    // has to be matched against the message in place and every number read
    // without building a string. The probe sees std::string allocations since
    // issue #697, so this assertion is not vacuous over paths that carry text.
    //
    // Arming the *timer* is out of scope here by design: `timerThread::Add`
    // builds a std::function, which is why `timerBridge` (#718) exists — the
    // requester writes atomics and the pool (or the control thread, off the
    // audio callback) does the allocating part. What is probed is everything
    // this object does itself.
    if (!TestHelpers::probeCountsAllocations()) return;
    REQUIRE(TestHelpers::probeSeesStringAllocations());

    Rig rig(std::to_string(kNeverFires));

    // Built outside the probe: it is the *handler* that must not allocate, not
    // the test's own construction of the message.
    const std::string stop = "stop";
    const std::string reset = "reset";
    const std::string unknown = "quantize 4n";
    const std::string number = "750";
    const std::string notevalue = "4n";
    const std::string ticks = "1440 ticks";
    const std::string clock = "clock main";
    const std::string bareclock = "clock";
    {
      TestHelpers::ProbeScope probe;
      rig.List(unknown);
      rig.List(reset);
      rig.List(stop);
      rig.List(clock);
      rig.List(bareclock);
      rig.List(number, 1);
      rig.List(notevalue, 1);
      rig.List(ticks, 1);
      rig.Int(250, 1);
      rig.Float(125.f, 1);
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }

    // And the probed messages really did something — an assertion that only
    // proves nothing happened proves nothing.
    CHECK(rig.obj.IntervalMs() == 125);
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("clocker: parameters survive a DumpJSON / ParseJSON round trip (#505)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here.
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_CLOCKER, "40");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"40\"") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(back->GetParams() == "40");

    SharedRecorder out;
    YSE::pHandle outHandle(&out);
    restored.Connect(back, 0, &outHandle, 0);

    // The restored interval is the one that runs: four reports at 40 ms cannot
    // arrive inside the 100 ms below, and at the 5 ms default they would.
    back->SetIntData(0, 1);
    CHECK_FALSE(waitFor([&] { return out.n() >= 4; }, 100));
    REQUIRE(waitFor([&] { return out.n() >= 2; }));
    CHECK(out.last() >= 20);

    back->SetIntData(0, 0);
  }

  // ─── the audio-callback route ───────────────────────────────────────────────

  TEST_CASE("clocker: a start delivered from inside Calculate still runs (#505)") {
    // The property a direct call structurally cannot show. A patcher message
    // handler runs on whichever thread dispatched it, and the deferred drain at
    // the top of `Calculate` dispatches *from the audio callback* — so this
    // chain (`.delay` -> `.clocker`) starts the object on the audio thread,
    // where arming the timer may not take `timerThread`'s mutex and has to go
    // through the bridge's wait-free front instead.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* clocker = p.CreateObject(YSE::OBJ::G_CLOCKER, "10");
    REQUIRE(clocker != nullptr);
    YSE::pHandle* del = p.CreateObject(YSE::OBJ::G_DELAY, "");
    REQUIRE(del != nullptr);

    SharedRecorder out;
    YSE::pHandle outHandle(&out);
    p.Connect(del, 0, clocker, 0);
    p.Connect(clocker, 0, &outHandle, 0);

    p.Calculate(YSE::T_DSP);
    CHECK(out.n() == 0);

    // The bang comes back out of the scheduler on the audio thread and starts
    // the clocker there. The arming itself is then deferred to the background
    // pool, so the first report lands a hop later rather than in the block that
    // started it — which is exactly the cost `timerBridge` trades for not
    // taking a mutex on the audio callback.
    del->SetBang(0);
    for (int i = 0; i < 40 && out.n() == 0; i++) {
      p.Calculate(YSE::T_DSP);
      std::this_thread::sleep_for(2ms);
    }

    REQUIRE(waitFor([&] { return out.n() >= 3; }));
    const std::vector<int> values = out.snapshot();
    for (std::size_t i = 1; i < values.size(); i++)
      CHECK(values[i] >= values[i - 1]);

    clocker->SetIntData(0, 0);
  }

} // TEST_SUITE

// ─── the beat engine (issue #725) ────────────────────────────────────────────
//
// Registered in the `clock` suite, not `patcher`: the cases below drive
// CLOCK::Manager().update() directly on the test thread to advance beats
// deterministically, which would race a live audio thread from another suite in
// the shared monolithic process. The same rig `.transport` (#513), `.timepoint`
// (#507) and `.tempo` (#512) use, and the same constants. No audio device
// required.

namespace {

  // 0.25 s per step at 120 BPM is exactly half a beat, and every number in that
  // sentence is exact in binary, so the cases assert on boundaries rather than
  // around them.
  constexpr float kStepSeconds = 0.25f;
  constexpr float kTempo = 120.f;

  // One block of the whole world, in the engine's own order: advance every
  // domain clock, then let the patcher render — which is where the scheduler
  // hands a beat wakeup that has come due to the object waiting on it.
  void Step(patcherImplementation& p) {
    YSE::CLOCK::Manager().update(kStepSeconds);
    p.Calculate(YSE::T_DSP);
  }

  // Records the beats outlet. Distinct from `Recorder` above because outlet 1
  // is a float outlet and the *values* are the claim.
  struct BeatRecorder : YSE::PATCHER::pObject {
    std::vector<double> values;

    BeatRecorder() : pObject(false) {
      values.reserve(1024);
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat(
          [this](float v, int, YSE::THREAD) { values.push_back((double)v); });
    }
    const char* Type() const override {
      return "clocker_beat_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    double last() const {
      return values.empty() ? -1.0 : values.back();
    }
  };

  // The same, for the one case that lets the *millisecond* timer deliver: those
  // sends arrive on the timerThread worker while the test thread reads them.
  struct SharedBeatRecorder : YSE::PATCHER::pObject {
    mutable std::mutex mtx;
    std::vector<double> values;
    std::atomic<int> count{0};

    SharedBeatRecorder() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
        {
          std::scoped_lock lock(mtx);
          values.push_back((double)v);
        }
        count.fetch_add(1, std::memory_order_release);
      });
    }
    const char* Type() const override {
      return "clocker_shared_beat_recorder";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    int n() const {
      return count.load(std::memory_order_acquire);
    }
    double last() const {
      std::scoped_lock lock(mtx);
      return values.empty() ? -1.0 : values.back();
    }
  };

  // A `.clocker` in a real patcher with a recorder on each outlet. No
  // `.transport` is built: this object never creates a clock and never writes
  // one, so the cases make their own.
  struct ClockRig {
    patcherImplementation patcher{1, nullptr};
    Recorder ms;
    BeatRecorder beats;
    YSE::pHandle msHandle{&ms};
    YSE::pHandle beatHandle{&beats};
    YSE::pHandle* obj = nullptr;

    explicit ClockRig(const std::string& args) {
      obj = patcher.CreateObject(YSE::OBJ::G_CLOCKER, args);
      REQUIRE(obj != nullptr);
      patcher.Connect(obj, 0, &msHandle, 0);
      patcher.Connect(obj, 1, &beatHandle, 0);
    }

    // Max's `clock <name>`. WaitIdle only makes the resolve *deterministic* —
    // it cannot conjure a clock that was never created.
    void Bind(const std::string& name) {
      obj->SetListData(0, "clock " + name);
      patcher.Clocks()->WaitIdle();
    }
    void Send(const std::string& message, int inlet = 0) {
      obj->SetListData((unsigned int)inlet, message);
    }
    void Start() {
      obj->SetIntData(0, 1);
    }
    void Stop() {
      obj->SetIntData(0, 0);
    }
    void Step() {
      ::Step(patcher);
    }
    void Steps(int n) {
      for (int i = 0; i < n; i++)
        Step();
    }
  };

  // A clock nobody is driving yet is not what these cases want: `.clocker`
  // never writes a tempo, so the clock arrives already running.
  void MakeClock(const std::string& name) {
    REQUIRE(YSE::CLOCK::Manager().createClock(name, kTempo));
  }

  // Drop a clock and let the manager retire it, so a case's name is free again
  // for the next run of the process.
  void DropClock(const std::string& name) {
    YSE::CLOCK::Manager().destroyClock(name);
    YSE::CLOCK::Manager().update(0.01f);
  }

} // namespace

TEST_SUITE("clock") {

  // ─── the grid ───────────────────────────────────────────────────────────────

  TEST_CASE("clocker: a note value interval reports on the clock it names (#725)") {
    // The object #725 is about, end to end: a `.clocker` whose interval is one
    // quarter note, bound to a running domain clock, reporting once a beat. The
    // millisecond interval is 100 seconds, so nothing here can be the timer.
    MakeClock("ck.grid");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.grid");
    CHECK(rig.patcher.Clocks()->BoundCount() == 1);

    rig.Steps(4);
    // Created stopped: dropping a `.clocker` into a patch reports nothing.
    CHECK(rig.ms.values.empty());
    CHECK(rig.beats.values.empty());

    rig.Start();
    // And nothing on the start either — Max documents an immediate output for
    // `metro` and documents none here.
    CHECK(rig.ms.values.empty());
    CHECK(rig.beats.values.empty());

    rig.Step(); // half a beat: not yet
    CHECK(rig.beats.values.empty());
    rig.Step(); // one beat
    REQUIRE(rig.beats.values.size() == 1);
    REQUIRE(rig.ms.values.size() == 1);
    CHECK(rig.beats.values[0] == doctest::Approx(1.0));

    rig.Steps(2);
    REQUIRE(rig.beats.values.size() == 2);
    CHECK(rig.beats.values[1] == doctest::Approx(2.0));

    // A stop ends both outlets, and the grid does not resume by itself.
    rig.Stop();
    rig.Steps(6);
    CHECK(rig.beats.values.size() == 2);
    CHECK(rig.ms.values.size() == 2);

    DropClock("ck.grid");
  }

  TEST_CASE("clocker: a tick-count interval is beats, not milliseconds (#725)") {
    // `1440 ticks` is three quarter notes — three beats — and the case that
    // falsifies a reader that took the leading number: 1440 *milliseconds* on
    // this rig would be the millisecond engine, which reports nothing here.
    MakeClock("ck.ticks");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("1440 ticks", 1);
    rig.Bind("ck.ticks");
    rig.Start();

    rig.Steps(5); // 2.5 beats
    CHECK(rig.beats.values.empty());
    rig.Step(); // 3.0
    REQUIRE(rig.beats.values.size() == 1);
    CHECK(rig.beats.values[0] == doctest::Approx(3.0));

    DropClock("ck.ticks");
  }

  TEST_CASE("clocker: the grid is read off the clock, so a late block reports once (#725)") {
    // The anti-drift claim, and the one place this object parts from `.metro`
    // and `.tempo`. A block that covered four beats is not four reports: the
    // number is the time *now*, and four copies of one reading are not the
    // reports that were missed. What must not happen is the other failure — a
    // run that counted its wakeups would report 1 here and stay three beats
    // behind for the rest of its life.
    MakeClock("ck.jump");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.jump");
    rig.Start();

    YSE::CLOCK::Manager().update(2.f); // four beats in one block
    rig.patcher.Calculate(YSE::T_DSP);
    REQUIRE(rig.beats.values.size() == 1);
    CHECK(rig.beats.values[0] == doctest::Approx(4.0));

    // And the grid is still on the beat rather than four behind it.
    rig.Step(); // 4.5
    CHECK(rig.beats.values.size() == 1);
    rig.Step(); // 5.0
    REQUIRE(rig.beats.values.size() == 2);
    CHECK(rig.beats.values[1] == doctest::Approx(5.0));

    DropClock("ck.jump");
  }

  TEST_CASE("clocker: a paused clock holds the run where it stands (#725)") {
    // A domain clock at tempo 0 never brings another grid point due, which is
    // the only honest reading of "one beat from now" on a clock that is not
    // moving — `.metro`'s rule for the same case.
    MakeClock("ck.pause");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.pause");
    rig.Start();
    rig.Steps(2);
    REQUIRE(rig.beats.values.size() == 1);

    YSE::CLOCK::Manager().setTempo("ck.pause", 0.f, 0.f);
    rig.Steps(20);
    CHECK(rig.beats.values.size() == 1);

    // And it picks straight up again when the domain does.
    YSE::CLOCK::Manager().setTempo("ck.pause", kTempo, 0.f);
    rig.Steps(2);
    CHECK(rig.beats.values.size() == 2);
    CHECK(rig.beats.values[1] == doctest::Approx(2.0));

    DropClock("ck.pause");
  }

  // ─── what the outlets carry ─────────────────────────────────────────────────

  TEST_CASE("clocker: a reset rewinds both outlets and leaves the grid alone (#725)") {
    // Max's "resets the elapsed time to 0 without stopping or restarting the
    // clock", carried onto the beat engine: the number falls back to measuring
    // from here, and the reports keep landing on the grid points they were
    // already on rather than being re-phased to the reset.
    MakeClock("ck.reset");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.reset");
    rig.Start();

    rig.Steps(4);
    REQUIRE(rig.beats.values.size() == 2);
    CHECK(rig.beats.values[1] == doctest::Approx(2.0));

    rig.Send("reset"); // standing exactly on beat 2
    rig.Steps(2); // beat 3: one beat after the reset, on the same grid
    REQUIRE(rig.beats.values.size() == 3);
    CHECK(rig.beats.values[2] == doctest::Approx(1.0));

    DropClock("ck.reset");
  }

  TEST_CASE("clocker: a millisecond interval on a bound clock still reports beats (#725)") {
    // The beats outlet is not gated on the *interval*: reporting a measurement
    // is not the same act as choosing when to report it. This is also where the
    // two outlets are shown to be two measurements rather than two spellings of
    // one — the milliseconds run on real time while the beats sit still,
    // because nothing has advanced the domain.
    MakeClock("ck.both");

    patcherImplementation p(1, nullptr);
    SharedRecorder ms;
    SharedBeatRecorder beats;
    YSE::pHandle msHandle(&ms);
    YSE::pHandle beatHandle(&beats);
    YSE::pHandle* obj = p.CreateObject(YSE::OBJ::G_CLOCKER, "5");
    REQUIRE(obj != nullptr);
    p.Connect(obj, 0, &msHandle, 0);
    p.Connect(obj, 1, &beatHandle, 0);

    obj->SetListData(0, "clock ck.both");
    p.Clocks()->WaitIdle();
    obj->SetIntData(0, 1);

    REQUIRE(waitFor([&] { return beats.n() >= 3; }));
    // Milliseconds have passed and beats have not, on the one run.
    CHECK(beats.last() == doctest::Approx(0.0));
    CHECK(waitFor([&] { return ms.last() > 0; }));

    // Move the domain two beats and the beats outlet follows it, still on the
    // millisecond timer's schedule.
    for (int i = 0; i < 4; i++)
      YSE::CLOCK::Manager().update(kStepSeconds);
    CHECK(waitFor([&] { return beats.last() == doctest::Approx(2.0); }));

    obj->SetIntData(0, 0);
    DropClock("ck.both");
  }

  // ─── binding, and the clock this object never creates ───────────────────────

  TEST_CASE("clocker: a bare 'clock' gives the millisecond clock back (#725)") {
    // Max: "sets the object back to using Max's regular millisecond clock." The
    // beat run has nothing left to count on, so it stops reporting; the
    // millisecond interval here is 100 seconds, so nothing else can fire.
    MakeClock("ck.unbind");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.unbind");
    rig.Start();
    rig.Steps(2);
    REQUIRE(rig.beats.values.size() == 1);

    rig.Send("clock");
    rig.Steps(10);
    CHECK(rig.beats.values.size() == 1);
    CHECK(rig.ms.values.size() == 1);

    DropClock("ck.unbind");
  }

  TEST_CASE("clocker: it never creates a clock, and starts when one appears (#725)") {
    // `.transport` is the sole creator (#513). A name nothing has claimed is
    // bound all the same and simply has no beats, and the bridge's own Poll
    // picks the clock up when something else makes it — so the run begins when
    // the clock starts existing rather than staying dead.
    const std::string name = "ck.late";
    REQUIRE_FALSE(YSE::CLOCK::Manager().clockExists(name));

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind(name);
    rig.Start();
    rig.Steps(8);

    CHECK_FALSE(YSE::CLOCK::Manager().clockExists(name));
    CHECK(rig.ms.values.empty());
    CHECK(rig.beats.values.empty());

    MakeClock(name);
    // Poll re-arms an unresolved binding every RESOLVE_INTERVAL_BLOCKS blocks,
    // so this is bounded by a couple of passes and not by the clock's tempo.
    for (int i = 0; i < 400 && rig.beats.values.size() < 2; i++)
      rig.Step();

    REQUIRE(rig.beats.values.size() >= 2);
    // The first report is where the beat measurement starts; the ones after it
    // rise from there.
    CHECK(rig.beats.values.back() > rig.beats.values.front());

    DropClock(name);
  }

  TEST_CASE("clocker: a start from inside Calculate runs the beat engine (#725)") {
    // The property a direct call structurally cannot show. A patcher message
    // handler runs on whichever thread dispatched it, and the deferred drain at
    // the top of `Calculate` dispatches *from the audio callback* — so this
    // chain (`.delay` -> `.clocker`) starts the object on the audio thread,
    // where taking the beat baseline may not touch the clock manager's mutex
    // and has to come off the bridge's wait-free read instead.
    MakeClock("ck.audio");

    ClockRig rig(std::to_string(kNeverFires));
    rig.Send("4n", 1);
    rig.Bind("ck.audio");

    YSE::pHandle* del = rig.patcher.CreateObject(YSE::OBJ::G_DELAY, "");
    REQUIRE(del != nullptr);
    rig.patcher.Connect(del, 0, rig.obj, 0);

    rig.Step();
    CHECK(rig.ms.values.empty());

    del->SetBang(0);
    for (int i = 0; i < 12 && rig.beats.values.empty(); i++)
      rig.Step();

    REQUIRE_FALSE(rig.beats.values.empty());
    REQUIRE_FALSE(rig.ms.values.empty());
    // The start took its baseline on the audio thread, and the first grid point
    // is exactly one beat after it whichever half-beat that landed on.
    CHECK(rig.beats.values[0] == doctest::Approx(1.0));

    DropClock("ck.audio");
  }

  // ─── persistence ────────────────────────────────────────────────────────────

  TEST_CASE("clocker: the beat interval survives a DumpJSON / ParseJSON round trip (#725)") {
    // Checked by *driving* the restored object rather than by reading the JSON
    // back: a parameter that survived the file but not the rebuild would pass a
    // string comparison and fail here. The millisecond argument is 100 seconds,
    // so only a restored beat interval can report at all.
    MakeClock("ck.json");

    YSE::patcher src;
    src.create(2);
    YSE::pHandle* obj = src.CreateObject(YSE::OBJ::G_CLOCKER, "100000 1");
    REQUIRE(obj != nullptr);

    const std::string json = src.DumpJSON();
    CHECK(json.find("\"100000 1\"") != std::string::npos);

    patcherImplementation restored(1, nullptr);
    restored.ParseJSON(json);
    REQUIRE(restored.Objects() == 1);

    YSE::pHandle* back = restored.GetHandleFromList(0);
    REQUIRE(back != nullptr);
    CHECK(back->GetParams() == "100000 1");

    Recorder ms;
    BeatRecorder beats;
    YSE::pHandle msHandle(&ms);
    YSE::pHandle beatHandle(&beats);
    restored.Connect(back, 0, &msHandle, 0);
    restored.Connect(back, 1, &beatHandle, 0);

    back->SetListData(0, "clock ck.json");
    restored.Clocks()->WaitIdle();
    back->SetIntData(0, 1);

    ::Step(restored);
    CHECK(beats.values.empty());
    ::Step(restored);
    REQUIRE(beats.values.size() == 1);
    CHECK(beats.values[0] == doctest::Approx(1.0));
    CHECK(ms.values.size() == 1);

    back->SetIntData(0, 0);
    DropClock("ck.json");
  }

} // TEST_SUITE

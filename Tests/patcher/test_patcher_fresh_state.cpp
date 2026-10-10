// A fresh patcher object must not depend on what its memory held (issue #958).
//
// Before C++20, a default-constructed std::atomic holds no defined value, and
// the engine builds as C++17. `~line` left two of them unset and rendered heap
// garbage until its first message (#951). #958 audited every patcher object for
// the same pattern. These cases keep the audit honest: each object is built over
// memory filled with a fixed byte pattern, so a member the constructor forgets
// reads as that pattern every time instead of whatever the allocator hands back.
//
// Two kinds of check, because not every object exposes its state:
//
//   - **documented defaults**: the timing and MIDI objects have accessors, so
//     each one is compared with the default its PARAM_DOC / Max page states.
//   - **pattern independence**: the GUI and DSP objects are fingerprinted (the
//     GUI value, or the first rendered block) over several patterns, and every
//     fingerprint must equal the one built over zeroed memory.
//
// No audio device required.

#include <doctest/doctest.h>
#include <cstring>
#include <new>
#include <sstream>
#include <string>

#include "dsp/buffer.hpp"
#include "patcher/filters/dVcf.h"
#include "patcher/filters/pBandpass.h"
#include "patcher/filters/pHighpass.h"
#include "patcher/filters/pLowpass.h"
#include "patcher/generatorObjects/dSaw.h"
#include "patcher/generatorObjects/pSine.h"
#include "patcher/genericObjects/pLine.h"
#include "patcher/guiObjects/gButton.h"
#include "patcher/guiObjects/gDial.h"
#include "patcher/guiObjects/gFloat.h"
#include "patcher/guiObjects/gIncDec.h"
#include "patcher/guiObjects/gInt.h"
#include "patcher/guiObjects/gItemList.h"
#include "patcher/guiObjects/gKSlider.h"
#include "patcher/guiObjects/gLabelSwitch.h"
#include "patcher/guiObjects/gMatrixCtrl.h"
#include "patcher/guiObjects/gMultiSlider.h"
#include "patcher/guiObjects/gNSlider.h"
#include "patcher/guiObjects/gNodes.h"
#include "patcher/guiObjects/gRSlider.h"
#include "patcher/guiObjects/gSlider.h"
#include "patcher/guiObjects/gToggle.h"
#include "patcher/guiObjects/gXYSlider.h"
#include "patcher/math/dClip.h"
#include "patcher/math/dMultiply.h"
#include "patcher/math/dSubstract.h"
#include "patcher/midi/mMakeNote.h"
#include "patcher/midi/mMidiIn.h"
#include "patcher/midi/mMidiXIn.h"
#include "patcher/midi/mMpe.h"
#include "patcher/midi/mPoly.h"
#include "patcher/sinks.hpp"
#include "patcher/time/gClocker.h"
#include "patcher/time/gDelay.h"
#include "patcher/time/gLine.h"
#include "patcher/time/gMetro.h"
#include "patcher/time/gPipe.h"
#include "patcher/time/gRateLimit.h"
#include "patcher/time/gSetClock.h"
#include "patcher/time/gTempo.h"
#include "patcher/time/gThresh.h"
#include "patcher/time/gTimepoint.h"
#include "patcher/time/gTransport.h"

using TestHelpers::BufferSink;

namespace {

  // 0x6E makes every float ~1.8e28 and every int 0x6E6E6E6E; 0xA5 makes floats
  // negative and ints negative. Zero is the control: it is what a forgotten
  // member most often happens to read in a debug build.
  constexpr unsigned char kPatterns[] = {0x00, 0x6E, 0xA5};

  // Constructs a T over heap memory filled with `pattern`, and destroys it with
  // the scope. On the heap because several objects (slot tables, key arrays)
  // are too large to put on the test's stack.
  template <typename T> class OverPattern {
  public:
    explicit OverPattern(unsigned char pattern)
      : raw(::operator new(sizeof(T), std::align_val_t(alignof(T)))) {
      std::memset(raw, pattern, sizeof(T));
      obj = new (raw) T();
    }
    ~OverPattern() {
      obj->~T();
      ::operator delete(raw, std::align_val_t(alignof(T)));
    }
    OverPattern(const OverPattern&) = delete;
    OverPattern& operator=(const OverPattern&) = delete;

    T* operator->() {
      return obj;
    }
    T& operator*() {
      return *obj;
    }

  private:
    void* raw;
    T* obj = nullptr;
  };

  // Everything the GUI protocol will say about the object's state.
  template <typename T> std::string GuiFingerprint(unsigned char pattern) {
    OverPattern<T> obj(pattern);
    std::string print = obj->GetGuiValue();
    const unsigned int count = obj->GetGuiValueCount();
    print += " #" + std::to_string(count);
    for (unsigned int i = 0; i < count; i++)
      print += " | " + obj->GetGuiValueAt(i);
    return print;
  }

  // The first block a DSP object renders, with `input` (if any) on inlet 0.
  // Printed as text so a mismatch shows the samples; a NaN or a 1e28 shows as
  // itself rather than failing a comparison silently.
  template <typename T> std::string DspFingerprint(unsigned char pattern, YSE::DSP::buffer* input) {
    OverPattern<T> obj(pattern);
    BufferSink sink;
    obj->ConnectOutlet(sink.GetInlet(0), 0);
    sink.ConnectInlet(obj->GetOutlet(0), 0);
    if (input != nullptr) obj->GetInlet(0)->SetBuffer(input, YSE::T_GUI);
    obj->Calculate(YSE::T_DSP);
    if (sink.received == nullptr) return "no output";
    std::ostringstream print;
    const float* p = sink.received->getPtr();
    for (unsigned int i = 0; i < sink.received->getLength(); i++)
      print << p[i] << ' ';
    return print.str();
  }

  template <typename T> void CheckGuiPatternFree(const char* name) {
    CAPTURE(name);
    const std::string zeroed = GuiFingerprint<T>(0x00);
    for (unsigned char pattern : kPatterns) {
      CAPTURE(static_cast<int>(pattern));
      CHECK(GuiFingerprint<T>(pattern) == zeroed);
    }
  }

  template <typename T> void CheckDspPatternFree(const char* name, YSE::DSP::buffer* input) {
    CAPTURE(name);
    const std::string zeroed = DspFingerprint<T>(0x00, input);
    for (unsigned char pattern : kPatterns) {
      CAPTURE(static_cast<int>(pattern));
      CHECK(DspFingerprint<T>(pattern, input) == zeroed);
    }
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("fresh state: timing objects start at their documented times (#958)") {
    using namespace YSE::PATCHER;
    for (unsigned char pattern : kPatterns) {
      CAPTURE(static_cast<int>(pattern));

      OverPattern<gLine> line(pattern);
      CHECK(line->Grain() == 20);
      CHECK(line->RampTime() == 0);
      CHECK(line->Value() == doctest::Approx(0.0));
      CHECK_FALSE(line->IsRunning());

      OverPattern<gSpeedlim> speedlim(pattern);
      OverPattern<gQlim> qlim(pattern);
      for (gRateLimitBase* obj :
           {static_cast<gRateLimitBase*>(&*speedlim), static_cast<gRateLimitBase*>(&*qlim)}) {
        CAPTURE(obj->Type());
        CHECK(obj->Interval() == 0);
        CHECK(obj->IntervalBeats() == 0.0);
        CHECK(obj->Threshold() == 0);
        CHECK(obj->ThresholdBeats() == 0.0);
        CHECK(obj->Quantize() == 0.0);
        CHECK_FALSE(obj->HasOutput());
      }
      CHECK(qlim->Usurp());
      CHECK_FALSE(qlim->IsHolding());

      OverPattern<gThresh> thresh(pattern);
      CHECK(thresh->Threshold() == 10);
      OverPattern<gQuickthresh> quick(pattern);
      CHECK(quick->Threshold() == 40);
      CHECK(quick->Fudge() == 10);
      CHECK(quick->Extension() == 20);
      CHECK_FALSE(quick->Extended());

      OverPattern<gMetro> metro(pattern);
      CHECK(metro->PeriodBeats() == 0.0);

      OverPattern<gClocker> clocker(pattern);
      CHECK(clocker->IntervalMs() == 5);
      CHECK(clocker->IntervalBeats() == 0.0);
      CHECK_FALSE(clocker->Running());

      OverPattern<gDelay> delay(pattern);
      CHECK(delay->DelayTime() == gDelay::DEFAULT_DELAY);
      CHECK(delay->DelayBeats() == 0.0);
      CHECK_FALSE(delay->IsPending());

      OverPattern<gPipe> pipe(pattern);
      CHECK(pipe->DelayTime() == gPipe::DEFAULT_DELAY);
      CHECK(pipe->PendingCount() == 0u);

      OverPattern<gTimepoint> timepoint(pattern);
      CHECK(timepoint->Target() == 0.0);
      CHECK(timepoint->IsActive());
      CHECK_FALSE(timepoint->Armed());

      OverPattern<gTempo> tempo(pattern);
      CHECK(tempo->WantedTempo() == 120.f);
      CHECK(tempo->Multiplier() == 1);
      CHECK(tempo->Division() == 16);
      CHECK_FALSE(tempo->Running());

      OverPattern<gSetClock> setclock(pattern);
      CHECK(setclock->WantedTempo() == 120.f);
      CHECK(setclock->WantedRamp() == 0.f);

      OverPattern<gTransport> transport(pattern);
      CHECK(transport->WantedTempo() == 120.f);
      CHECK(transport->WantedRamp() == 0.f);
      CHECK_FALSE(transport->Running());
    }
  }

  TEST_CASE("fresh state: MIDI objects start with their documented filters and values (#958)") {
    using namespace YSE::PATCHER;
    for (unsigned char pattern : kPatterns) {
      CAPTURE(static_cast<int>(pattern));

#if YSE_ENABLE_MIDI_DEVICE
      // The MIDI-in objects only exist where the engine has MIDI device I/O
      // (not on Android or macOS); see the guard in mMidiIn.h / mMidiXIn.h.
      // Channel 0 and controller -1 mean "every channel" / "every controller":
      // a garbage filter would silently drop all input.
      OverPattern<mNoteIn> notein(pattern);
      CHECK(notein->Port() == 0u);
      CHECK(notein->ChannelFilter() == 0);

      OverPattern<mCtlIn> ctlin(pattern);
      CHECK(ctlin->ChannelFilter() == 0);
      CHECK(ctlin->ControllerFilter() == -1);

      OverPattern<mXCtlIn> xctlin(pattern);
      CHECK(xctlin->ChannelFilter() == 0);
      CHECK(xctlin->ControllerFilter() == -1);
#endif

      OverPattern<mMakeNote> makenote(pattern);
      CHECK(makenote->Velocity() == mMakeNote::DEFAULT_VELOCITY);
      CHECK(makenote->Duration() == mMakeNote::DEFAULT_DURATION);
      CHECK(makenote->Pending() == 0u);

      OverPattern<mPoly> poly(pattern);
      CHECK(poly->Voices() == mPoly::DEFAULT_VOICES);
      CHECK_FALSE(poly->Steals());

      OverPattern<mMpeConfig> mpeconfig(pattern);
      CHECK(mpeconfig->Zone() == MPE_ZONE_LOWER);
      CHECK(mpeconfig->Members() == MPE_MEMBERS_MAX);

      OverPattern<mMpeParse> mpeparse(pattern);
      CHECK(mpeparse->Zone() == MPE_ZONE_LOWER);
      CHECK(mpeparse->Members() == MPE_MEMBERS_MAX);
    }
  }

  TEST_CASE("fresh state: GUI objects report the same state whatever their memory held (#958)") {
    using namespace YSE::PATCHER;
    CheckGuiPatternFree<gButton>("gButton");
    CheckGuiPatternFree<gToggle>("gToggle");
    CheckGuiPatternFree<gInt>("gInt");
    CheckGuiPatternFree<gFloat>("gFloat");
    CheckGuiPatternFree<gSlider>("gSlider");
    CheckGuiPatternFree<gDial>("gDial");
    CheckGuiPatternFree<gIncDec>("gIncDec");
    CheckGuiPatternFree<gRSlider>("gRSlider");
    CheckGuiPatternFree<gXYSlider>("gXYSlider");
    CheckGuiPatternFree<gNSlider>("gNSlider");
    CheckGuiPatternFree<gKSlider>("gKSlider");
    CheckGuiPatternFree<gMultiSlider>("gMultiSlider");
    CheckGuiPatternFree<gNodes>("gNodes");
    CheckGuiPatternFree<gMatrixCtrl>("gMatrixCtrl");
    CheckGuiPatternFree<gUMenu>("gUMenu");
    CheckGuiPatternFree<gRadioGroup>("gRadioGroup");
    CheckGuiPatternFree<gTab>("gTab");
    CheckGuiPatternFree<gLed>("gLed");
    CheckGuiPatternFree<gTextButton>("gTextButton");
  }

  TEST_CASE("fresh state: DSP objects render the same first block whatever their memory held "
            "(#958)") {
    using namespace YSE::PATCHER;
    YSE::DSP::buffer input(YSE::STANDARD_BUFFERSIZE);
    float* p = input.getPtr();
    for (unsigned int i = 0; i < input.getLength(); i++)
      p[i] = (i % 16 < 8) ? 0.5f : -1.5f; // strays past dClip's default [-1, 1]

    // Generators: the output depends only on the object's own parameters.
    CheckDspPatternFree<pLine>("pLine", nullptr);
    CheckDspPatternFree<pSine>("pSine", nullptr);
    CheckDspPatternFree<dSaw>("dSaw", nullptr);

    // Processors: fed the same block, so any difference is the object's state.
    CheckDspPatternFree<pLowpass>("pLowpass", &input);
    CheckDspPatternFree<pHighpass>("pHighpass", &input);
    CheckDspPatternFree<pBandpass>("pBandpass", &input);
    CheckDspPatternFree<dVcf>("dVcf", &input);
    CheckDspPatternFree<dClip>("dClip", &input);
    CheckDspPatternFree<dMultiply>("dMultiply", &input);
    CheckDspPatternFree<dSubstract>("dSubstract", &input);
  }
}

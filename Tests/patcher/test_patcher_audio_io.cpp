// Tests for the patcher's audio I/O objects after the MSP retrofit, issue
// #1043: ~dac and ~adc channel arguments, set and lists, the patcher's DSP
// switch (start / stop / a number), and ~ezdac / ~ezadc, the same objects
// with an on/off GUI value. There is no DSP class here (graph I/O, the
// two-layer rule is waived), so everything is shown in rendered patches:
// through patcherImplementation::Calculate for ~dac, and through
// DSP::patcherInsert with a host buffer for ~adc. Existing patches (no
// arguments, a hand-written file with "parms": "") must play as before.

#include <doctest/doctest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include "dsp/buffer.hpp"
#include "dsp/patcherInsert.hpp"
#include "headers/constants.hpp"
#include "patcher/genericObjects/pAdc.h"
#include "patcher/genericObjects/pDac.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "support/alloc_probe.hpp"

using YSE::STANDARD_BUFFERSIZE;
using YSE::PATCHER::patcherImplementation;

namespace {

  bool AllEqual(YSE::DSP::buffer& b, float value) {
    const float* p = b.getPtr();
    for (UInt i = 0; i < b.getLength(); ++i)
      if (p[i] != value) return false;
    return true;
  }

  // A host buffer of `channels` channels, channel c filled with c + 1.
  MULTICHANNELBUFFER HostBuffer(unsigned int channels) {
    MULTICHANNELBUFFER io;
    io.resize(channels);
    for (unsigned int c = 0; c < channels; ++c) {
      io[c].resize(STANDARD_BUFFERSIZE);
      io[c] = static_cast<float>(c + 1);
    }
    return io;
  }

  void Refill(MULTICHANNELBUFFER& io) {
    for (unsigned int c = 0; c < io.size(); ++c)
      io[c] = static_cast<float>(c + 1);
  }

  bool Listed(const char* name) {
    for (const auto& n : YSE::PATCHER::Register().AllNames())
      if (n == name) return true;
    return false;
  }

} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("audio io: registry, categories and shape without arguments (#1043)") {
    for (const char* name :
         {YSE::OBJ::D_DAC, YSE::OBJ::D_ADC, YSE::OBJ::D_EZDAC, YSE::OBJ::D_EZADC}) {
      CAPTURE(name);
      CHECK(Listed(name));
    }
    YSE::PATCHER::pEzdac ezdac;
    YSE::PATCHER::pEzadc ezadc;
    CHECK(std::string(ezdac.Type()) == "~ezdac");
    CHECK(std::string(ezadc.Type()) == "~ezadc");
    CHECK(ezdac.GetCategory() == YSE::PATCHER::pCategory::IO);
    CHECK(ezadc.GetCategory() == YSE::PATCHER::pCategory::IO);
    // Outside a patcher there is no switch to show; DSP reads as on.
    CHECK(ezdac.GetGuiValue() == "on");

    // In a patcher, no arguments means one pin per patcher channel, as ~dac
    // and ~adc always had (the ez variants match them).
    patcherImplementation p(3, nullptr);
    for (const char* name : {YSE::OBJ::D_DAC, YSE::OBJ::D_EZDAC}) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name, "");
      REQUIRE(h != nullptr);
      CHECK(h->GetInputs() == 3);
      CHECK(h->GetOutputs() == 0);
      CHECK(h->IsDSPInput(0));
    }
    for (const char* name : {YSE::OBJ::D_ADC, YSE::OBJ::D_EZADC}) {
      CAPTURE(name);
      YSE::pHandle* h = p.CreateObject(name, "");
      REQUIRE(h != nullptr);
      CHECK(h->GetInputs() == 1); // messages only
      CHECK_FALSE(h->IsDSPInput(0));
      REQUIRE(h->GetOutputs() == 3);
      CHECK(h->OutputDataType(2) == YSE::OUT_TYPE::BUFFER);
    }
    YSE::pHandle* ez = p.CreateObject(YSE::OBJ::D_EZDAC, "");
    CHECK(ez->GetGuiValue() == "on");
    CHECK_FALSE(ez->GuiValueIsSettable());
  }

  TEST_CASE("audio io: channel arguments shape the pins (#1043)") {
    patcherImplementation p(2, nullptr);
    CHECK(p.CreateObject(YSE::OBJ::D_DAC, "3 4")->GetInputs() == 2);
    CHECK(p.CreateObject(YSE::OBJ::D_DAC, "1")->GetInputs() == 1);
    CHECK(p.CreateObject(YSE::OBJ::D_EZDAC, "5 6 7")->GetInputs() == 3);
    // ~adc: several numbers are one outlet each; one number N alone is N
    // outlets on channels 1 to N.
    CHECK(p.CreateObject(YSE::OBJ::D_ADC, "3 4")->GetOutputs() == 2);
    CHECK(p.CreateObject(YSE::OBJ::D_ADC, "4")->GetOutputs() == 4);
    CHECK(p.CreateObject(YSE::OBJ::D_EZADC, "1")->GetOutputs() == 1);
    // A word is no channel, and the arguments are stored as given.
    YSE::pHandle* named = p.CreateObject(YSE::OBJ::D_DAC, "main");
    CHECK(named->GetInputs() == 2);
    CHECK(named->GetParams() == "main");
  }

  TEST_CASE("~dac: arguments route inlets to output channels (#1043)") {
    // [~sig 0.5] -> [~dac 3]: channel 3 of 4. [~sig 0.25] -> both inlets of
    // [~dac 2 2]: channel 2 hears both, summed. [~sig 1] -> [~dac 9 0]:
    // channels the patcher does not have are silent.
    patcherImplementation p(4, nullptr);
    YSE::pHandle* half = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* quarter = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* one = p.CreateObject(YSE::OBJ::D_SIG, "1");
    YSE::pHandle* three = p.CreateObject(YSE::OBJ::D_DAC, "3");
    YSE::pHandle* twoTwo = p.CreateObject(YSE::OBJ::D_DAC, "2 2");
    YSE::pHandle* nowhere = p.CreateObject(YSE::OBJ::D_DAC, "9 0");
    p.Connect(half, 0, three, 0);
    p.Connect(quarter, 0, twoTwo, 0);
    p.Connect(quarter, 0, twoTwo, 1);
    p.Connect(one, 0, nowhere, 0);
    p.Connect(one, 0, nowhere, 1);

    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.f));
    CHECK(AllEqual(p.output[1], 0.5f));
    CHECK(AllEqual(p.output[2], 0.5f));
    CHECK(AllEqual(p.output[3], 0.f));
  }

  TEST_CASE("~dac: existing patches play as before (#1043)") {
    // Built in code: no arguments, inlet n on channel n, several ~dacs add.
    patcherImplementation p(2, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* dac1 = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* dac2 = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(a, 0, dac1, 0);
    p.Connect(b, 0, dac1, 1);
    p.Connect(b, 0, dac2, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.75f));
    CHECK(AllEqual(p.output[1], 0.25f));

    // A hand-written file in the documented minimal form (file_format.rst),
    // with "parms": "" on the ~dac, still drives both channels.
    const std::string file = R"({
      "object 0": {
        "ID": 0, "type": "~sig", "parms": "0.5",
        "outputs": { "output 0": { "Count": 2,
          "0": { "Object": 1, "Inlet": 0 },
          "1": { "Object": 1, "Inlet": 1 } } }
      },
      "object 1": { "ID": 1, "type": "~dac", "parms": "" }
    })";
    patcherImplementation loaded(2, nullptr);
    loaded.ParseJSON(file);
    REQUIRE(loaded.Objects() == 2);
    loaded.Calculate(YSE::T_DSP);
    CHECK(AllEqual(loaded.output[0], 0.5f));
    CHECK(AllEqual(loaded.output[1], 0.5f));
    CHECK(loaded.DspOn());
  }

  TEST_CASE("~dac: set and lists re-route at runtime (#1043)") {
    patcherImplementation p(3, nullptr);
    YSE::pHandle* a = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* b = p.CreateObject(YSE::OBJ::D_SIG, "0.25");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "1 2");
    p.Connect(a, 0, dac, 0);
    p.Connect(b, 0, dac, 1);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
    CHECK(AllEqual(p.output[1], 0.25f));

    // set <ch> routes the inlet it arrives on.
    dac->SetListData(1, "set 3");
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
    CHECK(AllEqual(p.output[1], 0.f));
    CHECK(AllEqual(p.output[2], 0.25f));

    // A list routes the inlets from the first, on whichever inlet.
    dac->SetListData(1, "3 1");
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.25f));
    CHECK(AllEqual(p.output[2], 0.5f));

    // set with more numbers continues with the next inlets; 0 and a channel
    // the patcher lacks are silent.
    dac->SetListData(0, "set 0 7");
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.f));
    CHECK(AllEqual(p.output[1], 0.f));
    CHECK(AllEqual(p.output[2], 0.f));

    // A word that routes nothing (a status window here) changes nothing.
    dac->SetListData(0, "set 2 2");
    dac->SetListData(0, "open");
    dac->SetListData(0, "wclose");
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[1], 0.75f));
    CHECK(p.DspOn());
  }

  TEST_CASE("~dac: stop silences the patch and start resumes where it was (#1043)") {
    // [.m stop] and [.m start] -> [~dac], as a patch author wires them, and a
    // [~phasor] whose ramp shows that nothing computes while DSP is off.
    patcherImplementation p(1, nullptr);
    YSE::pHandle* ramp = p.CreateObject(YSE::OBJ::D_PHASOR, "100");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    YSE::pHandle* stop = p.CreateObject(YSE::OBJ::G_MESSAGE, "stop");
    YSE::pHandle* start = p.CreateObject(YSE::OBJ::G_MESSAGE, "start");
    YSE::pHandle* ez = p.CreateObject(YSE::OBJ::D_EZDAC, "");
    p.Connect(ramp, 0, dac, 0);
    p.Connect(stop, 0, dac, 0);
    p.Connect(start, 0, dac, 0);

    // The same ramp rendered without interruption, for reference.
    patcherImplementation ref(1, nullptr);
    YSE::pHandle* refRamp = ref.CreateObject(YSE::OBJ::D_PHASOR, "100");
    ref.Connect(refRamp, 0, ref.CreateObject(YSE::OBJ::D_DAC, ""), 0);

    p.Calculate(YSE::T_DSP);
    ref.Calculate(YSE::T_DSP);
    CHECK_FALSE(AllEqual(p.output[0], 0.f));

    stop->SetBang(0);
    CHECK_FALSE(p.DspOn());
    CHECK(ez->GetGuiValue() == "off");
    for (int i = 0; i < 3; ++i) {
      p.Calculate(YSE::T_DSP);
      CHECK(AllEqual(p.output[0], 0.f));
    }

    start->SetBang(0);
    CHECK(p.DspOn());
    CHECK(ez->GetGuiValue() == "on");
    p.Calculate(YSE::T_DSP);
    ref.Calculate(YSE::T_DSP);
    for (UInt i = 0; i < STANDARD_BUFFERSIZE; ++i) {
      CAPTURE(i);
      CHECK(p.output[0].getPtr()[i] == ref.output[0].getPtr()[i]);
    }
  }

  TEST_CASE("audio io: numbers, startwindow and the host's PassData work the switch (#1043)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* ez = p.CreateObject(YSE::OBJ::D_EZDAC, "");
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_EZADC, "");
    YSE::pHandle* ctl = p.CreateObject(YSE::OBJ::G_RECEIVE, "audio");
    p.Connect(sig, 0, ez, 0);
    p.Connect(ctl, 0, adc, 0);
    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io = HostBuffer(1);

    // A click on the button is the host sending the opposite of its value.
    ez->SetIntData(0, 0);
    CHECK(ez->GetGuiValue() == "off");
    CHECK(adc->GetGuiValue() == "off");
    insert.process(io);
    CHECK(AllEqual(io[0], 0.f)); // the insert is silent too

    ez->SetFloatData(0, 0.5f);
    CHECK(ez->GetGuiValue() == "on");
    Refill(io);
    insert.process(io);
    CHECK(AllEqual(io[0], 0.5f));

    adc->SetIntData(0, 0);
    CHECK(ez->GetGuiValue() == "off");
    adc->SetListData(0, "startwindow");
    CHECK(ez->GetGuiValue() == "on");

    // Through a [.r audio]: the value waits in the queue for the next block,
    // which delivers it before it renders, so that block already obeys it.
    p.PassData("stop", "audio");
    CHECK(ez->GetGuiValue() == "on");
    Refill(io);
    insert.process(io);
    CHECK(ez->GetGuiValue() == "off");
    CHECK(AllEqual(io[0], 0.f));
    p.PassData(1, "audio");
    Refill(io);
    insert.process(io);
    CHECK(ez->GetGuiValue() == "on");
    CHECK(AllEqual(io[0], 0.5f));
  }

  TEST_CASE("~adc: arguments and set route the host's channels (#1043)") {
    // A two-channel insert on a four-channel host: [~adc 4 3] -> [~dac].
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "4 3");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    p.Connect(adc, 0, dac, 0);
    p.Connect(adc, 1, dac, 1);
    YSE::DSP::patcherInsert insert(p);

    MULTICHANNELBUFFER io = HostBuffer(4);
    insert.process(io);
    CHECK(AllEqual(io[0], 4.f));
    CHECK(AllEqual(io[1], 3.f));
    CHECK(AllEqual(io[2], 3.f)); // past the graph's channels: dry
    CHECK(AllEqual(io[3], 4.f));

    // set <outlet> <channel>, both from 1.
    adc->SetListData(0, "set 2 1");
    Refill(io);
    insert.process(io);
    CHECK(AllEqual(io[0], 4.f));
    CHECK(AllEqual(io[1], 1.f));

    // A list routes the outlets from the first.
    adc->SetListData(0, "2 2");
    Refill(io);
    insert.process(io);
    CHECK(AllEqual(io[0], 2.f));
    CHECK(AllEqual(io[1], 2.f));

    // Channel 0, or one the host lacks, gives the outlet nothing.
    adc->SetListData(0, "set 1 0");
    adc->SetListData(0, "set 2 9");
    Refill(io);
    insert.process(io);
    CHECK(AllEqual(io[0], 0.f));
    CHECK(AllEqual(io[1], 0.f));
  }

  TEST_CASE("~adc: one number is that many channels from 1 (#1043)") {
    YSE::patcher p;
    p.create(3);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_EZADC, "3");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "3 2 1");
    REQUIRE(adc->GetOutputs() == 3);
    for (int i = 0; i < 3; ++i)
      p.Connect(adc, i, dac, i);
    YSE::DSP::patcherInsert insert(p);
    MULTICHANNELBUFFER io = HostBuffer(3);
    insert.process(io);
    CHECK(AllEqual(io[0], 3.f));
    CHECK(AllEqual(io[1], 2.f));
    CHECK(AllEqual(io[2], 1.f));
  }

  TEST_CASE("audio io: arguments are saved, set and the DSP state are not (#1043)") {
    YSE::patcher src;
    src.create(2);
    YSE::pHandle* sig = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_EZDAC, "2");
    REQUIRE(src.CreateObject(YSE::OBJ::D_ADC, "1 1") != nullptr);
    src.Connect(sig, 0, dac, 0);
    dac->SetListData(0, "set 1");
    dac->SetIntData(0, 0);
    CHECK(dac->GetGuiValue() == "off");
    const std::string json = src.DumpJSON();

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 3);
    YSE::pHandle* ldac = nullptr;
    YSE::pHandle* ladc = nullptr;
    for (unsigned int i = 0; i < loaded.Objects(); ++i) {
      YSE::pHandle* h = loaded.GetHandleFromList(i);
      if (std::string(h->Type()) == "~ezdac") ldac = h;
      if (std::string(h->Type()) == "~adc") ladc = h;
    }
    REQUIRE(ldac != nullptr);
    REQUIRE(ladc != nullptr);
    CHECK(ldac->GetParams() == "2");
    CHECK(ldac->GetInputs() == 1);
    CHECK(ladc->GetParams() == "1 1");
    CHECK(ladc->GetOutputs() == 2);
    CHECK(ldac->GetGuiValue() == "on");

    // The argument's channel 2, not the set's channel 1.
    YSE::DSP::patcherInsert insert(loaded);
    MULTICHANNELBUFFER io = HostBuffer(2);
    io[0] = 0.f;
    io[1] = 0.f;
    insert.process(io);
    CHECK(AllEqual(io[0], 0.f));
    CHECK(AllEqual(io[1], 0.5f));
  }

  TEST_CASE("audio io: a live re-parse keeps the patcher's channel count (#1043)") {
    patcherImplementation p(3, nullptr);
    YSE::pHandle* sig = p.CreateObject(YSE::OBJ::D_SIG, "0.5");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "2");
    p.Connect(sig, 0, dac, 0);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[1], 0.5f));

    // Back to no arguments: one inlet per patcher channel (3, not the
    // registry's 2), and the cord on inlet 0 survives onto channel 1.
    dac->SetParams("");
    CHECK(dac->GetInputs() == 3);
    p.Calculate(YSE::T_DSP);
    CHECK(AllEqual(p.output[0], 0.5f));
    CHECK(AllEqual(p.output[1], 0.f));

    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "1");
    adc->SetParams("");
    CHECK(adc->GetOutputs() == 3);
  }

  TEST_CASE("audio io: Clear turns a stopped patcher back on (#1043)") {
    patcherImplementation p(1, nullptr);
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "");
    dac->SetListData(0, "stop");
    CHECK_FALSE(p.DspOn());
    p.Clear();
    CHECK(p.DspOn());
  }

  TEST_CASE("audio io: messages and blocks allocate nothing on the audio thread (#1043)") {
    patcherImplementation p(2, nullptr);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_EZADC, "2 1");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_DAC, "1 2 2");
    p.Connect(adc, 0, dac, 0);
    p.Connect(adc, 1, dac, 1);
    p.Connect(adc, 1, dac, 2);
    YSE::PATCHER::pObject* dacObj = p.GetObjectFromID(dac->GetID());
    YSE::PATCHER::pObject* adcObj = p.GetObjectFromID(adc->GetID());
    MULTICHANNELBUFFER io = HostBuffer(2);
    p.ProcessAsInsert(io); // first block: anything lazily sized is sized now

    // Built before the probe: the strings themselves allocate.
    const std::vector<std::string> messages = {"set 2",       "3 1 2", "stop",   "start",
                                               "startwindow", "open",  "set 1 2"};
    {
      TestHelpers::ProbeScope probe;
      for (const std::string& m : messages) {
        dacObj->GetInlet(1)->SetMessage(m, YSE::T_DSP);
        adcObj->GetInlet(0)->SetMessage(m, YSE::T_DSP);
        dacObj->GetInlet(2)->SetList(m, YSE::T_DSP);
      }
      dacObj->GetInlet(0)->SetInt(1, YSE::T_DSP);
      adcObj->GetInlet(0)->SetFloat(1.f, YSE::T_DSP);
      for (int i = 0; i < 4; ++i)
        p.ProcessAsInsert(io);
      dacObj->GetInlet(0)->SetInt(0, YSE::T_DSP);
      p.ProcessAsInsert(io);
      dacObj->GetInlet(0)->SetInt(1, YSE::T_DSP);
      p.ProcessAsInsert(io);
      p.Calculate(YSE::T_DSP);
      (void)adcObj->GetGuiValue();
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(p.DspOn());
  }

  TEST_CASE("audio io: control threads switch and re-route while it renders (#1043)") {
    // Under TSan a plain (non-atomic) route or switch shows up here.
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* adc = p.CreateObject(YSE::OBJ::D_ADC, "");
    YSE::pHandle* dac = p.CreateObject(YSE::OBJ::D_EZDAC, "");
    p.Connect(adc, 0, dac, 0);
    p.Connect(adc, 1, dac, 1);
    YSE::DSP::patcherInsert insert(p);

    std::atomic<bool> done{false};
    std::thread control([&] {
      int i = 0;
      while (!done.load()) {
        dac->SetListData(0, (i % 2) != 0 ? "1 2" : "2 1");
        adc->SetListData(0, (i % 3) != 0 ? "start" : "stop");
        (void)dac->GetGuiValue();
        ++i;
      }
    });
    MULTICHANNELBUFFER io = HostBuffer(2);
    for (int block = 0; block < 2000; ++block) {
      Refill(io);
      insert.process(io);
      // Whatever the routes and switch: silence, or each channel 1 or 2, or
      // their sum when both inlets land on one channel.
      for (unsigned int c = 0; c < 2; ++c) {
        const float v = io[c].getPtr()[0];
        CHECK((v == 0.f || v == 1.f || v == 2.f || v == 3.f));
      }
    }
    done.store(true);
    control.join();
    dac->SetListData(0, "start");
    CHECK(dac->GetGuiValue() == "on");
  }

} // TEST_SUITE("patcher")

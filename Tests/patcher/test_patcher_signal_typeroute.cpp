// Tests for the ~typeroute patcher object (patcher/routing/dTypeRoute),
// issue #1135. A graph object with no DSP class: one inlet that takes a
// signal and every kind of message, the signal out the left outlet and the
// messages out the right one. These drive it the way a patch does: a signal
// cord and a .receive into the same inlet of a rendered patch after a JSON
// round trip, with the messages read back both by a ~sig on a dac channel and
// by sinks of every message kind.

#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/patcherImplementation.h"
#include "patcher/pEnums.h"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/routing/dTypeRoute.h"
#include "headers/constants.hpp"
#include "support/alloc_probe.hpp"
#include "sinks.hpp"

using TestHelpers::BufferSink;
using TestHelpers::MessageSink;
using TestHelpers::OrderSink;
using YSE::DSP::buffer;
using YSE::PATCHER::dTypeRoute;
using YSE::PATCHER::patcherImplementation;

namespace {
  constexpr unsigned int N = YSE::STANDARD_BUFFERSIZE;

  bool All(const buffer& b, float value) {
    const float* y = const_cast<buffer&>(b).getPtr();
    for (unsigned int i = 0; i < N; ++i)
      if (y[i] != value) return false;
    return true;
  }
} // namespace

TEST_SUITE("patcher") {

  TEST_CASE("~typeroute: registry and shape (#1135)") {
    YSE::patcher p;
    p.create(1);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::D_TYPEROUTE);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == "~typeroute");
    CHECK(h->GetInputs() == 1);
    REQUIRE(h->GetOutputs() == 2);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::BUFFER);
    CHECK(h->OutputDataType(1) == YSE::OUT_TYPE::ANY);

    bool listed = false;
    for (const auto& name : YSE::PATCHER::Register().AllNames())
      if (name == YSE::OBJ::D_TYPEROUTE) listed = true;
    CHECK(listed);

    dTypeRoute obj;
    CHECK(obj.IsDSPObject());
    CHECK(obj.GetCategory() == YSE::PATCHER::pCategory::ROUTING);
    // One inlet takes a signal and every message type.
    const unsigned int accepted = obj.GetInlet(0)->GetAcceptedTypes();
    CHECK((accepted & YSE::PATCHER::IT_BUFFER) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_FLOAT) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_INT) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_BANG) != 0u);
    CHECK((accepted & YSE::PATCHER::IT_LIST) != 0u);
  }

  TEST_CASE("~typeroute: a signal and messages into one inlet leave by their own outlets in a "
            "rendered patch, after a JSON round trip (#1135)") {
    // [~sig 0.25] and [.receive tr1135] both into ~typeroute's inlet; outlet
    // 0 to dac 0, outlet 1 to a [~sig] on dac 1, so a number that reaches the
    // message outlet shows up as a level on the second channel.
    std::string dump;
    unsigned int routeID = 0;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* dc = src.CreateObject(YSE::OBJ::D_SIG, "0.25");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "tr1135");
      YSE::pHandle* route = src.CreateObject(YSE::OBJ::D_TYPEROUTE, "");
      YSE::pHandle* level = src.CreateObject(YSE::OBJ::D_SIG, "0");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(dc != nullptr);
      REQUIRE(cmd != nullptr);
      REQUIRE(route != nullptr);
      REQUIRE(level != nullptr);
      REQUIRE(dac != nullptr);
      src.Connect(dc, 0, route, 0);
      src.Connect(cmd, 0, route, 0);
      src.Connect(route, 0, dac, 0);
      src.Connect(route, 1, level, 0);
      src.Connect(level, 0, dac, 1);
      routeID = route->GetID();
      dump = src.DumpJSON();
    }

    // The sinks outlive the patcher (sinks.hpp, #967).
    OrderSink kinds;
    MessageSink words;
    YSE::pHandle kindsHandle(&kinds);
    YSE::pHandle wordsHandle(&words);
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    YSE::pHandle* route = dst.GetHandleFromID(routeID);
    REQUIRE(route != nullptr);
    REQUIRE(std::string(route->Type()) == "~typeroute");
    dst.Connect(route, 1, &kindsHandle, 0);
    dst.Connect(route, 1, &wordsHandle, 0);

    // The signal alone: it passes unchanged, and no message is sent.
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.25f));
    CHECK(All(dst.output[1], 0.f));
    dst.Calculate(YSE::T_DSP);
    CHECK(kinds.count == 0);

    // A float: out the message outlet (the ~sig on dac 1 takes it), and the
    // signal outlet is untouched.
    REQUIRE(dst.PassData(0.5f, "tr1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.25f));
    CHECK(All(dst.output[1], 0.5f));
    CHECK(kinds.lastKind == OrderSink::FLOAT);
    CHECK(kinds.lastFloat == 0.5f);
    CHECK(kinds.count == 1);

    // An int stays an int.
    REQUIRE(dst.PassData(3, "tr1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.25f));
    CHECK(All(dst.output[1], 3.f));
    CHECK(kinds.lastKind == OrderSink::INT);
    CHECK(kinds.lastInt == 3);

    // A bang stays a bang.
    REQUIRE(dst.PassBang("tr1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(kinds.lastKind == OrderSink::BANG);
    CHECK(kinds.count == 3);

    // A list passes as it was spelled; the ~sig reads its first number.
    REQUIRE(dst.PassData(std::string("0.75 2 3"), "tr1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.25f));
    CHECK(All(dst.output[1], 0.75f));
    CHECK(kinds.lastKind == OrderSink::LIST);
    CHECK(kinds.lastList == "0.75 2 3");

    // A word message stays that message: an object with a command channel
    // reads it as one, an object with only a list handler as a list.
    REQUIRE(dst.PassData(std::string("foo 1 2"), "tr1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(words.gotMessage);
    CHECK(words.received == "foo 1 2");
    CHECK(kinds.lastKind == OrderSink::LIST);
    CHECK(kinds.lastList == "foo 1 2");
    CHECK(kinds.count == 5);
    CHECK(All(dst.output[0], 0.25f));
  }

  TEST_CASE("~typeroute: unconnected, the signal outlet is silent whatever numbers arrive; "
            "signal cords sum (#1135, #1036)") {
    std::string dump;
    {
      patcherImplementation src(2, nullptr);
      YSE::pHandle* a = src.CreateObject(YSE::OBJ::D_SIG, "0.25");
      YSE::pHandle* b = src.CreateObject(YSE::OBJ::D_SIG, "0.5");
      YSE::pHandle* lone = src.CreateObject(YSE::OBJ::D_TYPEROUTE, "");
      YSE::pHandle* summed = src.CreateObject(YSE::OBJ::D_TYPEROUTE, "");
      YSE::pHandle* cmd = src.CreateObject(YSE::OBJ::G_RECEIVE, "lone1135");
      YSE::pHandle* dac = src.CreateObject(YSE::OBJ::D_DAC, "");
      REQUIRE(lone != nullptr);
      REQUIRE(summed != nullptr);
      src.Connect(cmd, 0, lone, 0);
      src.Connect(lone, 0, dac, 0);
      src.Connect(a, 0, summed, 0);
      src.Connect(b, 0, summed, 0);
      src.Connect(summed, 0, dac, 1);
      dump = src.DumpJSON();
    }
    patcherImplementation dst(2, nullptr);
    dst.ParseJSON(dump);
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.f));
    CHECK(All(dst.output[1], 0.75f));
    // A number is a message: it does not become the constant an unconnected
    // signal inlet reads.
    REQUIRE(dst.PassData(0.9f, "lone1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.f));
    REQUIRE(dst.PassData(4, "lone1135", YSE::T_GUI));
    dst.Calculate(YSE::T_DSP);
    CHECK(All(dst.output[0], 0.f));
  }

  TEST_CASE("~typeroute: a message never recomputes the signal side, and nothing on the audio "
            "thread allocates (#1135)") {
    BufferSink signalOut;
    OrderSink messageOut;
    dTypeRoute route;
    TestHelpers::Wire(route, 0, signalOut, 0);
    TestHelpers::Wire(route, 1, messageOut, 0);
    buffer in(N);
    in = 0.5f;
    const std::string list = "1 2 3";
    const std::string word = "foo 1";

    // The buffer that arrived is passed on as it is.
    route.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
    route.Calculate(YSE::T_DSP);
    CHECK(signalOut.received == &in);

    // Messages on the audio thread: forwarded, and the cold inlet does not
    // send the signal again.
    signalOut.received = nullptr;
    route.GetInlet(0)->SetFloat(0.5f, YSE::T_DSP);
    route.GetInlet(0)->SetInt(2, YSE::T_DSP);
    route.GetInlet(0)->SetBang(YSE::T_DSP);
    route.GetInlet(0)->SetList(list, YSE::T_DSP);
    route.GetInlet(0)->SetMessage(word, YSE::T_DSP);
    CHECK(signalOut.received == nullptr);
    CHECK(messageOut.count == 5);
    CHECK(messageOut.lastList == "foo 1");

    {
      TestHelpers::ProbeScope probe;
      for (int block = 0; block < 4; ++block) {
        route.ResetDSP();
        route.GetInlet(0)->SetFloat(0.25f, YSE::T_DSP);
        route.GetInlet(0)->SetInt(1, YSE::T_DSP);
        route.GetInlet(0)->SetBang(YSE::T_DSP);
        route.GetInlet(0)->SetList(list, YSE::T_DSP);
        route.GetInlet(0)->SetMessage(word, YSE::T_DSP);
        route.Calculate(YSE::T_DSP);
        route.GetInlet(0)->SetBuffer(&in, YSE::T_DSP);
        route.Calculate(YSE::T_DSP);
      }
      CHECK(TestHelpers::g_alloc_count.load() == 0);
    }
    CHECK(signalOut.received == &in);
  }

} // TEST_SUITE("patcher")

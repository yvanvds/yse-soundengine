// Shared sink objects for patcher unit tests.
//
// A "sink" is a tiny pObject subclass with a single inlet that records the
// last value received from an upstream outlet, so a test can assert on what
// the object-under-test sent.  Each sink type matches one outlet data type
// (FLOAT, INT, BUFFER).
//
// Originally extracted from test_patcher_math.cpp:24-55.

#pragma once

#include "patcher/pObject.h"
#include "dsp/buffer.hpp"

namespace TestHelpers {

struct FloatSink : YSE::PATCHER::pObject {
    float received = 0.f;
    bool  gotFloat = false;
    FloatSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterFloat([this](float v, int, YSE::THREAD) {
            received = v;
            gotFloat = true;
        });
    }
    const char* Type() const override { return "float_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
};

struct IntSink : YSE::PATCHER::pObject {
    int  received = -999;
    bool gotInt   = false;
    IntSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterInt([this](int v, int, YSE::THREAD) {
            received = v;
            gotInt   = true;
        });
    }
    const char* Type() const override { return "int_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
};

struct BufferSink : YSE::PATCHER::pObject {
    YSE::DSP::buffer* received = nullptr;
    BufferSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBuffer([this](YSE::DSP::buffer* b, int, YSE::THREAD) {
            received = b;
        });
    }
    const char* Type() const override { return "buffer_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
};

struct BangSink : YSE::PATCHER::pObject {
    int  bangCount = 0;
    bool gotBang   = false;
    BangSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang([this](int, YSE::THREAD) {
            bangCount++;
            gotBang = true;
        });
    }
    const char* Type() const override { return "bang_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
};

struct ListSink : YSE::PATCHER::pObject {
    std::string received;
    bool        gotList = false;
    ListSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterList([this](const std::string & v, int, YSE::THREAD) {
            received = v;
            gotList  = true;
        });
    }
    const char* Type() const override { return "list_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}
};

// Captures the inlet::SetMessage path (outlet::SendMessage -> obj->SetMessage).
// Used to test gMessage which sends via SendMessage rather than SendList.
struct MessageSink : YSE::PATCHER::pObject {
    std::string received;
    bool        gotMessage = false;
    MessageSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
    }
    const char* Type() const override { return "message_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string & message, float) override {
        received   = message;
        gotMessage = true;
    }
};

// Captures all four non-DSP message kinds.  Useful for verifying which path a
// switching/routing object (gGate, gRoute, gSwitch) actually fires.
struct MultiSink : YSE::PATCHER::pObject {
    bool        gotBang  = false;
    bool        gotInt   = false;
    bool        gotFloat = false;
    bool        gotList  = false;
    int         intValue   = 0;
    float       floatValue = 0.f;
    std::string listValue;

    MultiSink() : pObject(false) {
        inputs.emplace_back(this, true, 0);
        inputs.back().RegisterBang ([this](int, YSE::THREAD)               { gotBang  = true; });
        inputs.back().RegisterInt  ([this](int v, int, YSE::THREAD)        { gotInt   = true; intValue   = v; });
        inputs.back().RegisterFloat([this](float v, int, YSE::THREAD)      { gotFloat = true; floatValue = v; });
        inputs.back().RegisterList ([this](const std::string & v, int, YSE::THREAD) { gotList = true; listValue = v; });
    }
    const char* Type() const override { return "multi_sink"; }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
        gotBang = gotInt = gotFloat = gotList = false;
        intValue = 0;
        floatValue = 0.f;
        listValue.clear();
    }
};

} // namespace TestHelpers

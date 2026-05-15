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

} // namespace TestHelpers

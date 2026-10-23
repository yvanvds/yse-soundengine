#pragma once

#include "dTrig.h"
#include "../../dsp/operators/polar.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for ``~cartopol`` and ``~poltocar`` (issue #1053),
     *         on ``DSP::cartToPolar`` / ``DSP::polarToCart``.
     *
     *  Ported from Max's ``cartopol~`` and ``poltocar~``, which take no
     *  arguments, messages or attributes.
     *
     *  - Left inlet: the first coordinate (real part, or magnitude), a signal
     *    or a number.
     *  - Right inlet: the second coordinate (imaginary part, or phase), a
     *    signal or a number. Its number is a parameter (creation argument 1,
     *    default 0), so it survives a save.
     *  - Two signal outlets. Max's ``cartopol~`` phase outlet is optional (it
     *    skips the atan2 when unconnected); here it is always computed.
     *
     *  Numbers are used while no signal is connected; a connected signal wins
     *  (#1036). The wrapper contains no sample math.
     */
    class dPolarBase : public pObject {
    public:
      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetLeftFloat(float value, int inlet, YSE::THREAD thread);
      void SetLeftInt(int value, int inlet, YSE::THREAD thread);
      void SetRightFloat(float value, int inlet, YSE::THREAD thread);
      void SetRightInt(int value, int inlet, YSE::THREAD thread);

    protected:
      dPolarBase(); // RT-cold — constructor use.

      // Fills in the documentation that differs per direction. RT-cold.
      void Document(const char* summary, const char* leftLabel, const char* leftDoc,
                    const char* rightLabel, const char* rightDoc, const char* out0Label,
                    const char* out0Doc, const char* out0Range, const char* out1Label,
                    const char* out1Doc, const char* out1Range, const char* paramDoc);

      virtual void Process(DSP::buffer& left, Flt right) = 0;
      virtual void Process(DSP::buffer& left, DSP::buffer& right) = 0;
      virtual DSP::buffer& Left() = 0;
      virtual DSP::buffer& Right() = 0;

    private:
      aFlt leftFloatIn;
      aFlt rightFloatIn;
    };

    /** @brief ``~cartopol``: real / imaginary to magnitude / phase. */
    class dCarToPolBase : public dPolarBase {
    protected:
      void Process(DSP::buffer& left, Flt right) override {
        fn(left, right);
      }
      void Process(DSP::buffer& left, DSP::buffer& right) override {
        fn(left, right);
      }
      DSP::buffer& Left() override {
        return fn.magnitude();
      }
      DSP::buffer& Right() override {
        return fn.phase();
      }

    private:
      DSP::cartToPolar fn;
    };

    /** @brief ``~poltocar``: magnitude / phase to real / imaginary. */
    class dPolToCarBase : public dPolarBase {
    protected:
      void Process(DSP::buffer& left, Flt right) override {
        fn(left, right);
      }
      void Process(DSP::buffer& left, DSP::buffer& right) override {
        fn(left, right);
      }
      DSP::buffer& Left() override {
        return fn.real();
      }
      DSP::buffer& Right() override {
        return fn.imaginary();
      }

    private:
      DSP::polarToCart fn;
    };

    SIGNAL_TRIG_CLASS(dCarToPol, dCarToPolBase, YSE::OBJ::D_CARTOPOL)
    SIGNAL_TRIG_CLASS(dPolToCar, dPolToCarBase, YSE::OBJ::D_POLTOCAR)

    /** @brief ``~phasewrap``: wraps a phase into -pi..pi, on ``DSP::phaseWrap``. */
    class dPhaseWrapBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::phaseWrap fn;
    };

    SIGNAL_TRIG_CLASS(dPhaseWrap, dPhaseWrapBase, YSE::OBJ::D_PHASEWRAP)

  } // namespace PATCHER
} // namespace YSE

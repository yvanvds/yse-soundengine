#pragma once

#include "dTrig.h"
#include "../../dsp/math.hpp"
#include "../../dsp/operators/units.hpp"

#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief Shared body for ``~mtof`` and ``~ftom`` (issue #1052), on
     *         ``DSP::midiToFreq`` / ``DSP::freqToMidi``.
     *
     *  Ported from Max's ``mtof~`` and ``ftom~``.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). ``base <Hz>`` and ``ref <note>`` set the tuning.
     *  - Parameters: ``base`` (creation argument 1, default 440) and ``ref``
     *    (creation argument 2, default 69), so the tuning survives a save.
     *
     *  Max's ``base`` and ``ref`` attributes map onto the parameters and
     *  messages. Its Scala tuning attributes (``map``, ``mapname``, ``mid``,
     *  ``scale``, ``scalename``) are not ported: they load tuning files. The
     *  wrapper contains no sample math.
     */
    class dPitchConvertBase : public pObject {
    public:
      void SetMessage(const std::string& message, float value) override;
      bool HandlesMessages() const override {
        return true;
      }
      void Calculate(YSE::THREAD thread) override;

      void SetFloat(float value, int inlet, YSE::THREAD thread);
      void SetInt(int value, int inlet, YSE::THREAD thread);

    protected:
      dPitchConvertBase(); // RT-cold — constructor use.

      // Fills in the documentation that differs per direction. RT-cold.
      void Document(const char* summary, const char* inletDoc, const char* inletRange,
                    const char* outletDoc, const char* outletRange);

      virtual DSP::buffer& Process(DSP::buffer& in, float base, float ref) = 0;

    private:
      aFlt floatIn;
      aFlt base;
      aFlt ref;
    };

    /** @brief ``~mtof``: MIDI note to Hz, on ``DSP::midiToFreq``. */
    class dMtofBase : public dPitchConvertBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in, float base, float ref) override {
        return fn.setBase(base).setRef(ref)(in);
      }

    private:
      DSP::midiToFreq fn;
    };

    /** @brief ``~ftom``: Hz to MIDI note, on ``DSP::freqToMidi``. */
    class dFtomBase : public dPitchConvertBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in, float base, float ref) override {
        return fn.setBase(base).setRef(ref)(in);
      }

    private:
      DSP::freqToMidi fn;
    };

    SIGNAL_TRIG_CLASS(dMtof, dMtofBase, YSE::OBJ::D_MTOF)
    SIGNAL_TRIG_CLASS(dFtom, dFtomBase, YSE::OBJ::D_FTOM)

    /** @brief ``~atodb``: amplitude to dBFS, on ``DSP::ampToDb``. */
    class dAmpToDbBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::ampToDb fn;
    };

    /** @brief ``~dbtoa``: dBFS to amplitude, on ``DSP::dbToAmp``. */
    class dDbToAmpBase : public dTrigBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }

    private:
      DSP::dbToAmp fn;
    };

    SIGNAL_TRIG_CLASS(dAtoDb, dAmpToDbBase, YSE::OBJ::D_ATODB)
    SIGNAL_TRIG_CLASS(dDbtoA, dDbToAmpBase, YSE::OBJ::D_DBTOA)

    /**
     *  @brief Shared body for ``~mstosamps`` and ``~sampstoms`` (issue
     *         #1052), on ``DSP::msToSamples`` / ``DSP::samplesToMs``.
     *
     *  Ported from Max's ``mstosamps~`` and ``sampstoms~``. Both follow the
     *  live sample rate.
     *
     *  - Inlet: the signal, or a number used while no signal is connected
     *    (#1036). A number is also converted at once and sent from the right
     *    outlet; a list is converted element by element and sent from the
     *    right outlet as a list (Max's ``float``, ``int`` and ``list``).
     *  - Left outlet: the converted signal. Right outlet: converted numbers.
     *
     *  A list keeps at most ``LIST_MAX`` numbers. Nothing on these paths
     *  allocates: the list reply is built in memory reserved at
     *  construction. The wrapper contains no sample math.
     */
    class dTimeConvertBase : public pObject {
    public:
      static constexpr int LIST_MAX = 64;

      void SetMessage(const std::string&, float) override {}
      void Calculate(YSE::THREAD thread) override;

      void SetFloat(float value, int inlet, YSE::THREAD thread);
      void SetInt(int value, int inlet, YSE::THREAD thread);
      void SetList(const std::string& value, int inlet, YSE::THREAD thread);

    protected:
      dTimeConvertBase(); // RT-cold — constructor use.

      // Fills in the documentation that differs per direction. RT-cold.
      void Document(const char* summary, const char* inletDoc, const char* outletDoc,
                    const char* numberDoc);

      virtual DSP::buffer& Process(DSP::buffer& in) = 0;
      virtual float Convert(float value) const = 0;

    private:
      aFlt floatIn;
      std::string reply;
    };

    /** @brief ``~mstosamps``: milliseconds to samples. */
    class dMsToSampsBase : public dTimeConvertBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }
      float Convert(float value) const override {
        return DSP::MsToSamples(value);
      }

    private:
      DSP::msToSamples fn;
    };

    /** @brief ``~sampstoms``: samples to milliseconds. */
    class dSampsToMsBase : public dTimeConvertBase {
    protected:
      DSP::buffer& Process(DSP::buffer& in) override {
        return fn(in);
      }
      float Convert(float value) const override {
        return DSP::SamplesToMs(value);
      }

    private:
      DSP::samplesToMs fn;
    };

    SIGNAL_TRIG_CLASS(dMsToSamps, dMsToSampsBase, YSE::OBJ::D_MSTOSAMPS)
    SIGNAL_TRIG_CLASS(dSampsToMs, dSampsToMsBase, YSE::OBJ::D_SAMPSTOMS)

  } // namespace PATCHER
} // namespace YSE

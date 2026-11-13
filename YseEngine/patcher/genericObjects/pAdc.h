#pragma once
#include "pAudioIO.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~adc``: audio input into a patcher graph, the mirror of pDac.
     *
     *  Where pDac is a DSP sink that collects the graph's per-channel output,
     *  pAdc is a DSP start point that injects an externally-supplied buffer
     *  per outlet into the graph. It is the entry point that lets a patcher
     *  run as a channel / sound insert (issue #167): the host adapter
     *  (patcherImplementation::ProcessAsInsert) points each outlet at the
     *  host channel Route() names just before the graph renders.
     *
     *  - Arguments (issue #1043): input channels, from 1, one outlet each:
     *    ``~adc 3 4`` has two outlets carrying channels 3 and 4. One number N
     *    alone is N outlets on channels 1 to N. Without one, one outlet per
     *    patcher channel, outlet *n* on channel *n*, as before.
     *  - Inlet 0 (messages only): a number (0 turns the patcher's DSP off,
     *    anything else on), ``start``, ``startwindow``, ``stop``;
     *    ``set <outlet> <channel>`` routes one outlet (both from 1; channel 0
     *    is none); a list of numbers routes the outlets from the first.
     *  - An outlet whose channel the host buffer does not have sends nothing.
     *
     *  Not ported: a symbol argument (a named mixer input), and ``open``,
     *  ``wclose`` and the double-click (an audio status window the host owns).
     */
    class pAdc : public pAudioIO {
    public:
      pAdc();
      explicit pAdc(int channels);
      const char* Type() const override {
        return YSE::OBJ::D_ADC;
      }
      CREATE(pAdc)
      _NO_MESSAGES
      _DO_CALCULATE

      _INT_IN(SetSwitchInt)
      _FLOAT_IN(SetSwitchFloat)
      _LIST_IN(SetList)

      _PARM_CLEAR
      _PARM_PARSE

      // Point one outlet at an externally-owned buffer, borrowed for the
      // duration of the next Calculate(). A null pointer means "no input for
      // this outlet this block" (the outlet sends nothing). Out-of-range
      // outlets are ignored. Called by the host adapter on the audio thread;
      // a plain pointer store, so RT-safe.
      void SetChannelBuffer(unsigned int outlet, YSE::DSP::buffer* buffer);

      // Number of audio outlets (== NumRoutes()).
      unsigned int NumChannels() const;

    private:
      // Rebuilds the outlets from the arguments. Construction only.
      void ShapePorts();

      // Borrowed, externally-owned input buffers, one per outlet. Deliberately
      // not cleared by ResetDSP: the host adapter sets these immediately
      // before Calculate(), and Calculate() runs ResetDSP() on every object
      // first, so clearing here would wipe the freshly injected input before
      // it is read.
      std::vector<DSP::buffer*> channels;
    };

    /**
     *  @brief ``~ezadc``: ``~adc`` as an on/off button (issue #1043).
     *
     *  The same object, plus a GUI value: ``on`` or ``off``, the patcher's
     *  DSP state. It is read-only; a click is a number or ``start`` /
     *  ``stop`` sent by the host. Drawing (colours, style) is the host's.
     */
    class pEzadc : public pAdc {
    public:
      pEzadc();
      explicit pEzadc(int channels);
      const char* Type() const override {
        return YSE::OBJ::D_EZADC;
      }
      CREATE(pEzadc)
      std::string GetGuiValue() override;

    private:
      void Describe();
    };

  } // namespace PATCHER
} // namespace YSE

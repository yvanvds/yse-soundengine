#pragma once
#include "pAudioIO.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~dac``: the patch's audio output, the DSP sink that collects
     *         the graph's result for the host. The mirror of pAdc.
     *
     *  - Arguments: output channels, from 1, one inlet each (issue #1043):
     *    ``~dac 3 4`` sends its two inlets to channels 3 and 4. Without one,
     *    one inlet per patcher channel, inlet *n* on channel *n*, as before.
     *  - Inlets: one signal each. Every ``~dac`` in the patch is summed into
     *    the output (#932), as are two inlets routed to one channel.
     *  - Messages, on any inlet: a number (0 turns the patcher's DSP off,
     *    anything else on), ``start``, ``startwindow``, ``stop``; ``set <ch>``
     *    routes that inlet (more numbers route the inlets after it); a list
     *    of numbers routes the inlets from the first.
     *
     *  Not ported: a symbol argument (a named mixer output), and ``open``,
     *  ``wclose`` and the double-click, which open an audio status window: the
     *  host owns that. They are ignored.
     */
    class pDac : public pAudioIO {
    public:
      pDac();
      explicit pDac(int channels);
      const char* Type() const override {
        return YSE::OBJ::D_DAC;
      }
      CREATE(pDac)
      _NO_MESSAGES
      _DO_RESET
      _NO_CALCULATE

      _BUFFER_IN(SetBuffer)
      _INT_IN(SetSwitchInt)
      _FLOAT_IN(SetSwitchFloat)
      _LIST_IN(SetList)

      _PARM_CLEAR
      _PARM_PARSE

      // The buffer that reached inlet `inlet` this block, or null. Used by
      // the patcher implementation, which routes it with Route(inlet).
      YSE::DSP::buffer* GetBuffer(unsigned int inlet);

    private:
      // Rebuilds the inlets from the arguments. Construction only.
      void ShapePorts();

      std::vector<DSP::buffer*> channels;
    };

    /**
     *  @brief ``~ezdac``: ``~dac`` as an on/off button (issue #1043).
     *
     *  The same object, plus a GUI value: ``on`` or ``off``, the patcher's
     *  DSP state. It is read-only; a click is a number or ``start`` /
     *  ``stop`` sent by the host. Drawing (colours, style) is the host's.
     */
    class pEzdac : public pDac {
    public:
      pEzdac();
      explicit pEzdac(int channels);
      const char* Type() const override {
        return YSE::OBJ::D_EZDAC;
      }
      CREATE(pEzdac)
      std::string GetGuiValue() override;

    private:
      void Describe();
    };

  } // namespace PATCHER
} // namespace YSE

/*
  ==============================================================================

    fmVoice.hpp
    DX7-class 6-operator FM synthesiser voice for YSE::synth (issue #176).

    A SYNTH::dspVoice subclass that renders one DX7 voice by driving the FM
    core ported from music-synthesizer-for-android (see dsp/fm/msfa/). The tone
    is described by a shared, live-editable fmPatch (the same contract the DX7
    SysEx importer #177 targets); all voices of one synth share one patch and
    each keeps its own independent operator / envelope / LFO state.

    The MSFA core is held behind a PIMPL so its fixed-point macros (N, LG_N, …)
    never leak into this header or into consumers/tests. All allocation happens
    in the constructor / clone() (setup thread); process() is allocation-free.

    See docs/design/synth_core.md §3 for the voice contract.

  ==============================================================================
*/

#ifndef YSE_DSP_FM_FMVOICE_HPP
#define YSE_DSP_FM_FMVOICE_HPP

#include <memory>

#include "fmPatch.hpp"
#include "../../synth/dspVoice.hpp"

namespace YSE {
  namespace SYNTH {

    /// @cond INTERNAL
    // Opaque audio-thread state (MSFA Dx7Note + Lfo + controllers + scratch).
    // Defined in fmVoice.cpp so the fixed-point core stays out of this header.
    struct fmVoiceState;
    /// @endcond

    /**
     *  @brief DX7-class 6-operator FM voice.
     *
     *  Construct one, dial in its ``fmPatch`` (or load a DX7 voice into it via
     *  #177), then hand it to ``synth::addVoices``. Clones share the patch and
     *  stay editable through ``patch()``. Patch edits take effect on the next
     *  note-on (the FM core bakes operator parameters at key-down, exactly like
     *  the hardware reacting to a program change between notes).
     */
    class API fmVoice : public dspVoice {
    public:
      /** @brief Construct a voice with the built-in sine test patch. */
      fmVoice(int outputChannels = 1);

      ~fmVoice() override;

      /** @brief The shared, live-editable patch. Retain to keep editing after the prototype is
       * gone. */
      std::shared_ptr<fmPatch> patch() const {
        return params;
      }

      /** @brief Convenience reference to the shared patch. */
      fmPatch& parameters() {
        return *params;
      }

      /** @brief Overwrite the shared patch; applied on the next note-on. */
      fmVoice& setPatch(const fmPatch& p) {
        *params = p;
        return *this;
      }

      // ---- dspVoice contract -------------------------------------------------

      /** @brief Render one block, honouring and settling ``intent``. Audio-thread only. */
      void process(SOUND_STATUS& intent) override;

      /** @brief Return a new voice sharing this voice's patch, with fresh DSP state. Setup-thread
       * only. */
      dspVoice* clone() override;

    protected:
      /** @brief Copy-construct: share the patch, build fresh independent core state. */
      fmVoice(const fmVoice& other);

    private:
      void startNote();

      std::shared_ptr<fmPatch> params;
      std::unique_ptr<fmVoiceState> state;
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_DSP_FM_FMVOICE_HPP

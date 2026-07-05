/*
  ==============================================================================

    midifile.h
    Created: 12 Jul 2014 6:55:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILE_H_INCLUDED
#define MIDIFILE_H_INCLUDED

#include <string>
#include "../synth/synth.hpp" // YSE::synth (for connect/disconnect)
#include "../headers/defines.hpp"

namespace YSE {
  namespace MIDI {
    /// @cond INTERNAL
    class fileImpl;
    /// @endcond

    /**
     *  @brief Standard MIDI file playback.
     *
     *  Load a ``.mid`` file with ``create``, then control playback with
     *  ``play`` / ``pause`` / ``stop``.
     */
    class API file {
    public:
      file();
      ~file();

      /** @brief Load a standard MIDI file.
       *  @return ``true`` on success.
       */
      bool create(const std::string& fileName);

      /** @brief Start or resume playback. */
      void play();

      /** @brief Pause playback. Resume with ``play``. */
      void pause();

      /** @brief Stop playback and rewind to the start. */
      void stop();

      /** @brief Route this file's playback into a synth.
       *
       *  While the file plays, every note / controller / pitch-bend event it
       *  contains is delivered to ``synth`` (block-accurately, on the audio
       *  thread). May be called for several synths to drive them together.
       *
       *  @warning ``synth`` must outlive the connection: ``disconnect`` it (or
       *           destroy this file) before destroying the synth. */
      void connect(YSE::synth& synth);

      /** @brief Stop routing this file's playback into ``synth``. */
      void disconnect(YSE::synth& synth);

    private:
      fileImpl* pimpl;
    };
  } // namespace MIDI
} // namespace YSE

#endif // MIDIFILE_H_INCLUDED

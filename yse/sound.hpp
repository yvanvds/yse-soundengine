/*
  ==============================================================================

    sound.h
    Created: 28 Jan 2014 11:50:15am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUND_H_INCLUDED
#define SOUND_H_INCLUDED

#include "classes.hpp"
#include "headers/defines.hpp"
#include "headers/types.hpp"

namespace YSE {

  /**
      A sound object is needed for every kind of sound you want to use. Sounds can use
      audio files (wav, ogg, flac, and more on some systems) or can be linked to a DSP
      source. Sounds can be mono, stereo or multichannel.

      If a soundfile is streamed, it will use its own soundbuffer. Otherwise YSE will
      create a new buffer only if needed or re-use and existing buffer if another file
      with the same filename has already been loaded. Unloading of soundfiles and clearing
      memory is done internally: when a buffer is not used by any sound any more, it will be 
      flagged for deletion.
  */

  class API sound {
  public:
    /** Create a sound and register it with the soundsystem. Other functions will not work
        as long as a sound hasn't been created. In debug mode, an assertion will be called if
        you try to do so. On the other hand, create cannot be called twice. If you need 
        another sound, create a new sound object.

        @param fileName   This can be an absolute path or something relative to the 
                          working directory.
        @param channel    The sound channel you want to attach the sound to. Sounds can
                          be moved to other channels at any time. If no channel is provided,
                          the sound will be added to the global channel.
        @param loop       Use true to loop this sound continuously. If not provided, the sound
                          will not loop.
        @param volume     The initial volume to play this sound. This should be a value between
                          0.0 and 1.0. Default is 1.0.
        @param streaming  Whether or not this sound should be streamed. Defaults to false.
                          Streaming is mainly intended for big audio files that are only used
                          by a single sound. When a sound is used repeatedly, avoid streaming.
    */
    sound& create(const char * fileName, const channel * const ch = NULL, Bool loop = false, Flt volume = 1.0f, Bool streaming = false);
    sound& create(DSP::dspSourceObject &  dsp, const channel * const ch = NULL, Flt volume = 1.0f);

    sound& pos     (const Vec &v    ); Vec  pos     ();
    sound& spread3D(      Flt  value); Flt  spread3D();
    sound& speed   (      Flt  value); Flt  speed   (); // default = 1; negative speed plays a sound backwards. Streams have negative speeds set to zero because they would cause a lot of disk usage.
    sound& size    (      Flt  value); Flt  size    ();
    sound& loop    (      Bool value); Bool loop    ();

    sound& volume  (Flt value, UInt time = 0); Flt volume(); // can be used as a fader if time (ms) > 0
    sound& fadeAndStop(UInt time); // fade out and stop sound

    sound& play   (); Bool playing(); 
    sound& pause  (); Bool paused ();
    sound& stop   (); Bool stopped();
    sound& toggle ();
    sound& restart();
    sound& time   (Flt value); Flt  time  (); // set get current time in samples
                               UInt length(); // sound length in samples

    sound& relative(Bool value); Bool relative(); // set/get position & angle relative to player (can also be used for '2D' sounds)
    sound& doppler (Bool value); Bool doppler (); // enable doppler effect on this sound
    sound& pan2D   (Bool value); Bool pan2D   (); // shortcut to set sound to relative, pos(0), without doppler

    Bool streamed(); // returns true if the sound is streamed from disk instead of loaded into memory
    Bool ready   (); // indicates wether a non-streaming sound is loaded into memory and ready to be played
    Bool valid   (); // returns false if no sound is loaded

    sound& dsp(DSP::dspObject & value); DSP::dspObject * dsp(); // attach a dsp object to this sound

    sound& occlusion(Bool value); Bool occlusion(); // enable or disable occlusion on this sound

    sound& release(); // manually release sound

    sound();
   ~sound();
  private:
    INTERNAL::soundImplementation *pimpl;
  };

}




#endif  // SOUND_H_INCLUDED

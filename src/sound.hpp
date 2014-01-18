#pragma once
#include "utils/vector.hpp"
#include "dsp/dsp.hpp"
#include "channel.hpp" 

namespace YSE {
  class soundimpl ; // internal use only

  class API sound {
  public:
    sound& create   (const char * fileName, const channel * const ch = NULL, Bool loop = false, Flt volume = 1.0f, Bool streaming = false);
    sound& create   (DSP::dspSource &  dsp     , const channel * const ch = NULL,                    Flt volume = 1.0f);

    sound& pos      (const Vec &v     ); Vec pos      ();
    sound& spread3D (      Flt  value ); Flt spread3D ();
    sound& speed    (      Flt  value ); Flt speed    (); // default = 1; negative speed plays a sound backwards. Streams have negative speeds set to zero because they would cause a lot of disk usage.
    sound& size     (      Flt  value ); Flt size     ();
    sound& loop     (      Bool value ); Bool loop    ();

    sound& volume   (Flt value, UInt time = 0); Flt volume(); // can be used as a fader if time (ms) > 0
    sound& fadeAndStop(UInt time); // fade out and stop sound

    sound& play   (UInt latency = 0); Bool playing(); // latency is used for better timing in software synths, but can be disregarded for normal use
    sound& pause  (UInt latency = 0); Bool paused ();
    sound& stop   (UInt latency = 0); Bool stopped();
    sound& toggle (UInt latency = 0);
    sound& restart(UInt latency = 0);
    sound& time   (Flt value); Flt  time  (); // set get current time in samples
                               UInt length(); // sound length in samples
    
                               

    sound& relative (Bool value = true); Bool relative(); // set/get position & angle relative to player (can also be used for '2D' sounds)
    sound& doppler  (Bool value = true); Bool doppler (); // enable doppler effect on this sound
    sound& pan2D    (Bool value = true); Bool pan2D   (); // shortcut to set sound to relative, pos(0), without doppler

    Bool streamed (); // returns true if the sound is streamed from disk instead of loaded into memory
    Bool ready    (); // indicates wether a non-streaming sound is loaded into memory and ready to be played
    Bool valid    (); // returns false if no sound is loaded

    sound& dsp(DSP::dsp & value); DSP::dsp * dsp(); // attach a dsp object to this sound

    sound& occlusion(Bool value); Bool occlusion(); // enable or disable occlusion on this sound

    sound& release(); // manually release sound

    sound();   
   ~sound();
  private:
    soundimpl *pimpl;
  };

}

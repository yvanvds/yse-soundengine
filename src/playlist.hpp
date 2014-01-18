#pragma once
#include <vector>
#include <string>
#include "utils/vector.hpp"
#include "dsp/dsp.hpp"
#include "channel.hpp" 


namespace YSE {
  class playlistImpl; // internal use only

  class API playlist {
    // a playlist is a list of sounds that can be played alternating or at random
    // mostly it behaves just as a normal sound, but the defaults are different.
    // Specifically, sounds in a playlist are by default 2D and streamed from disk.
  public:
    playlist& add(const char *  filename); // add file to list

    void clear  (); // remove all files from list
    Int elms    (); // number of list elements
    Int current (); // currently active element, -1 if none is active

    playlist& pos      (const Vec &v     ); Vec pos      ();
    playlist& spread3D (      Flt  value ); Flt spread3D ();
    playlist& volume   (      Flt  value ); Flt volume   ();
    playlist& speed    (      Flt  value ); Flt speed    (); // default = 1; negative speed plays a sound backwards. Streams have negative speeds set to zero because they would cause a lot of disk usage.
    playlist& size     (      Flt  value ); Flt size     ();
    playlist& output   (channel& ch); channel& output();

    playlist& play  (Int nr = -1); // start playing, nr allows you to choose a specific song from the list
    playlist& pause (           );
    playlist& stop  (           );
    
    Bool playing();
    Bool paused ();
    Bool stopped();

    Flt time  (); // position in current song (in seconds)
    Flt length(); // length of current song (in seconds)

    playlist& autoloop (Bool value); Bool autoloop(); // loop through sounds, default: true
    playlist& relative (Bool value); Bool relative(); // set/get position & angle relative to player (can also be used for '2D' sounds)
    playlist& doppler  (Bool value); Bool doppler (); // enable doppler effect on this sound
    playlist& pan2D    (Bool value); Bool pan2D   (); // shortcut to set sound to relative, pos(0), without doppler

    playlist& streamed (Bool value); Bool streamed (); // returns true if the sound is streamed from disk instead of loaded into memory
    playlist& occlusion(Bool value); Bool occlusion(); // enable or disable occlusion on this sound


    playlist();
   ~playlist();
  private:
    playlistImpl * pimpl;
  };
}

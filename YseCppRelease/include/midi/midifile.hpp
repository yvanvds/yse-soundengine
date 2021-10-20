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
//#include "../synth/synth.hpp"
#include "../headers/defines.hpp"

namespace YSE {
  namespace MIDI {
    class fileImpl;
    
    class API file {
    public:
      file();
      ~file();
      bool create(const std::string & fileName);

      // connected synths will read from this midifile,
      // next to parsing their own midi input.
      // Mind that to play the file you also need to:
      // 1. use the play() function below
      // 2. use the play() function on the sound which is 
      //    connected to the synth
      //void connect   (synth * player);
      //void disconnect(synth * player);
      
      void play ();
      void pause();
      void stop ();

    private:
      fileImpl * pimpl;
    };
  }
}



#endif  // MIDIFILE_H_INCLUDED

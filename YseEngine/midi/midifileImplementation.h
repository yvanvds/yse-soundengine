/*
  ==============================================================================

    midifileImplementation.h
    Created: 12 Jul 2014 7:09:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILEIMPLEMENTATION_H_INCLUDED
#define MIDIFILEIMPLEMENTATION_H_INCLUDED

#include <string>
#include "../internalHeaders.h"
//#include "../synth/synthImplementation.h"
#include <forward_list>

namespace YSE {
  namespace MIDI {

    class fileImpl {
    public:
      fileImpl(file * head);
      ~fileImpl();

      bool create(const std::string & fileName);

      void play ();
      void pause();
      void stop ();

      //void connect   (synth * player);
      //void disconnect(synth * player);

      //void getMessages(MidiBuffer & incomingMidi);

      // this is called only by a synth destructor which still
      // has pointers to this file acive
      //void removeDevice(SYNTH::implementationObject * player);
      void removeInterface();
      bool hasInterface();

    private:
      // interface object
      file * head;

      //MidiFile midiFile;
      //MidiMessageSequence sequence;

      std::atomic<SOUND_STATUS> intent;

      bool hasFile;
      //int startSample;

      //std::forward_list<YSE::SYNTH::implementationObject *> readers;
    };

  }
}



#endif  // MIDIFILEIMPLEMENTATION_H_INCLUDED

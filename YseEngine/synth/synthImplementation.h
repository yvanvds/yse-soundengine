/*
  ==============================================================================

    synthImplementation.h
    Created: 6 Jul 2014 10:01:29pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYNTHIMPLEMENTATION_H_INCLUDED
#define SYNTHIMPLEMENTATION_H_INCLUDED

#include "../classes.hpp"
#include "../utils/lfQueue.hpp"
#include "../dsp/dspObject.hpp"
#include "samplerSound.h"
#include "synthInterface.hpp"
#include <forward_list>
#include "../midi/midifileImplementation.h"
//
//namespace YSE {
//  namespace SYNTH {
//
//    class implementationObject : public DSP::dspSourceObject {
//    public:
//      implementationObject(interfaceObject * head);
//     ~implementationObject();
//
//      void addVoices(const samplerConfig & voice, int numVoices);
//      void addVoices(dspVoice * voice, int numVoices, int channel, int lowestNote, int highestNote);
//
//      virtual void process(SOUND_STATUS & intent);
//      virtual void frequency(Flt value);
//
//      void sendMessage(const messageObject & message);
//      bool sync();
//      void parseMessage(const messageObject & message);
//
//      void registerMidiFile(MIDI::fileImpl * file);
//      void removeMidiFile  (MIDI::fileImpl * file);
//
//      void removeInterface();
//      bool hasInterface();
//
//    private:
//      MidiMessageCollector midiCollector;
//      MidiKeyboardState keyboardState;
//      Synthesiser synthesizer;
//      AudioSampleBuffer synthBuffer;
//
//      interfaceObject * head;
//      lfQueue<messageObject> messages;
//      int ID;
//      int voiceID;
//
//      // callback on note events
//      void(*onNoteEvent)(bool, float*, float*);
//
//      std::forward_list<MIDI::fileImpl *> midiFiles;
//    };
//
//  }
//}
//


#endif  // SYNTHIMPLEMENTATION_H_INCLUDED

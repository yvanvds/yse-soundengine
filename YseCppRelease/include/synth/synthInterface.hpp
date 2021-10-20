/*
  ==============================================================================

    synthInterface.h
    Created: 6 Jul 2014 10:02:00pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYNTHINTERFACE_HPP_INCLUDED
#define SYNTHINTERFACE_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "dspVoice.hpp"
#include "../music/note.hpp"
#include <string>

//namespace YSE {
//  // don't mind this (it's needed for friend declaration)
//  namespace MIDI {
//    class fileImpl;
//  }
//
//  namespace SYNTH {
//
//    class API samplerConfig {
//    public:
//      samplerConfig();
//
//      samplerConfig & name(const char * name);
//      samplerConfig & file(const char * file);
//
//      // the midi channel for which to accepts events (default to 0, which is omni)
//      samplerConfig & channel(U8 channel);
//
//      // the note to map the default playing speed to (default = 60)
//      samplerConfig & root(U8 rootNote);
//
//      // the lowest and highest note this sample should play (default 0 - 128)
//      samplerConfig & range(U8 low, U8 high);
//
//      // attack time, release time and maximum length in seconds
//      // default is 0, 0.1, 10
//      samplerConfig & envelope(Flt attack, Flt release, Flt maxLength);
//
//      const std::string & name() const { return _name; }
//      const std::string & file() const { return _file; }
//      U8 channel     () const { return _channel    ; }
//      U8 root        () const { return _rootNote   ; }
//      U8 lowestNote  () const { return _lowestNote ; }
//      U8 highestNote () const { return _highestNote; }
//      Flt attackTime () const { return _attackTime ; }
//      Flt releaseTime() const { return _releaseTime; }
//      Flt maxLength  () const { return _maxLength  ; }
//
//    private:
//      std::string _name;
//      std::string _file;
//      U8  _channel    ; 
//      U8  _rootNote   ; 
//      U8  _lowestNote ; 
//      U8  _highestNote; 
//      Flt _attackTime ; 
//      Flt _releaseTime; 
//      Flt _maxLength  ; 
//    };
//
//    class API interfaceObject  {
//    public:
//      interfaceObject();
//     ~interfaceObject();
//
//      // Sound interfaces cannot be copied. The implementation needs access to the 
//      // interface object. To do this, the address of the interface must not change.
//      interfaceObject(const interfaceObject&) = delete;
//
//      interfaceObject & create();
//      interfaceObject & addVoices(const samplerConfig & voice, int numVoices);
//      //interfaceObject & addVoices(dspVoice * voice, int numVoices, int channel, int lowestNote=0, int highestNote=128);
//
//      // sounds are recognized by the name you give them in a samplerConfig
//      // when you add them
//      interfaceObject & removeSound(const std::string & name);
//
//      interfaceObject & noteOn (int channel, int noteNumber, float velocity);
//      interfaceObject & noteOff(int channel, int noteNumber);
//
//      interfaceObject & noteOn (const MUSIC::note & note);
//      interfaceObject & noteOff(const MUSIC::note & note);
//
//      interfaceObject & pitchWheel(int channel                , int  value);
//      interfaceObject & controller(int channel, int number    , int  value);
//      interfaceObject & aftertouch(int channel, int noteNumber, int  value);
//      interfaceObject & sustain   (int channel,                 bool value);
//      interfaceObject & sostenuto (int channel,                 bool value);
//      interfaceObject & softPedal (int channel,                 bool value);
//
//      // pass 0 as channel to turn off all notes on all channels
//      interfaceObject & allNotesOff(int channel);
//
//      // Callback for note events. The event can be changed by the callback. Make sure
//      // the noteNumber stays between 0-127 and velocity between 0-1. This function will
//      // be called from the DSP thread, so be sure to make your own variables atomic.
//      interfaceObject & onNoteEvent(void(*func)(bool noteOn, float * noteNumber, float * velocity));
//
//      int getNumVoices() { return numVoices; }
//
//    private:
//      implementationObject * pimpl;
//      int numVoices;
//      friend class implementationObject;
//      friend class sound;
//      friend class MIDI::fileImpl;
//    };
//
//  }
//}



#endif  // SYNTHINTERFACE_H_INCLUDED


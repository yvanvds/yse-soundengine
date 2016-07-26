/*
  ==============================================================================

    motifImplementation.h
    Created: 14 Apr 2015 6:19:01pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIFIMPLEMENTATION_H_INCLUDED
#define MOTIFIMPLEMENTATION_H_INCLUDED


#include "../../classes.hpp"
#include "../../utils/lfQueue.hpp"

namespace YSE {
  namespace MOTIF {

    class implementationObject {
    public:
      implementationObject(motif * head);
      ~implementationObject();

      bool update();
      void parseMessage(const messageObject & message);
      inline void sendMessage(const messageObject & message) {
        messages.push(message);
      }

      void removeInterface();

      SCALE::implementationObject * getValidPitches();
      UInt getNotes(Flt startPos, Flt endPos, std::vector<MUSIC::pNote>::iterator & firstElement);

      UInt size() { return notes.size(); }
      Flt getLength() { return length; }

      MUSIC::pNote & getNote(UInt pos);

    private:
      void sort();

      std::atomic<motif *> head;
      Flt length;
      std::vector<MUSIC::pNote> notes;
      Bool needsSorting;
      SCALE::implementationObject * validPitches;

      lfQueue<messageObject> messages;
    };

  }
}




#endif  // MOTIFIMPLEMENTATION_H_INCLUDED

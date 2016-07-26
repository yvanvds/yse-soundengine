/*
  ==============================================================================

    scaleImplementation.h
    Created: 14 Apr 2015 2:54:18pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALEIMPLEMENTATION_H_INCLUDED
#define SCALEIMPLEMENTATION_H_INCLUDED

#include "../../classes.hpp"
#include "../../utils/lfQueue.hpp"

namespace YSE {
  namespace SCALE {

    class implementationObject {
    public:
      implementationObject(scale * head);
      ~implementationObject();

      bool update();
      void parseMessage(const messageObject & message);
      inline void sendMessage(const messageObject & message) {
        messages.push(message);
      }

      void removeInterface();

      /* TODO: the next functions are doubles of the interface functions. 
         Is there a way to avoid this?
      */

      // Add a pitch to the scale. By default it will be added at every
      // octave, but this can be changed with the step value. (A step <= 0
      // means adding only this exact pitch, without transpositions.)
      void add(Flt pitch, Flt step = 12);

      // Remove a pitch from the scale. By default it will be removed at every
      // octave, but this can be changed with the step value.
      void remove(Flt pitch, Flt step = 12);

      // Scales should be sorted before they are used.
      void sort();

      // Check if a pitch is part of the scale
      Bool has(Flt pitch);

      // Get the nearest match for this pitch
      Flt getNearest(Flt pitch) const;

      // count notes in this scale
      UInt size() const;

      // remove all notes
      void clear();

    private:
      std::atomic<scale *> head;
      std::vector<Flt> pitches;
      Bool needsSorting;

      lfQueue<messageObject> messages;

      friend class scale;
      friend class SCALE::managerObject;
    };

  }
}



#endif  // SCALEIMPLEMENTATION_H_INCLUDED

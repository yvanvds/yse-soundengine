/*
  ==============================================================================

    channel.h
    Created: 23 Mar 2014 11:50:29am
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNEL_H_INCLUDED
#define CHANNEL_H_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.
  */

  class channel; // interface object

  namespace CHANNEL {
    class implementationObject;
    class messageObject;
    class managerObject;
    enum MESSAGE {
      VOLUME,
      MOVE,
      VIRTUAL,
      ATTACH_REVERB,
      ATTACH_DSP,
      // Send/return bus wiring (issue #165, design docs/design/send_return_buses.md).
      // All applied on the audio thread in parseMessage() as pointer/scalar
      // writes — no allocation, no locking. Wiring-time validation (cycle
      // rejection, generation indexing) runs on the control thread before these
      // are posted.
      ADD_SEND, // (re)point a send slot at a return + link its back-reference
      SEND_LEVEL, // set a slot's target level (ramped, click-free)
      REMOVE_SEND, // detach a send slot and unlink its back-reference
      SET_GENERATION, // update a return's processing-order generation index
    };
  } // namespace CHANNEL
} // namespace YSE

#endif // CHANNEL_H_INCLUDED

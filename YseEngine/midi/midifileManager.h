/*
  ==============================================================================

    midifileManager.h
    Created: 13 Jul 2014 5:21:20pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MIDIFILEMANAGER_H_INCLUDED
#define MIDIFILEMANAGER_H_INCLUDED

#include "midifileImplementation.h"
#include "midifile.hpp"

namespace YSE {
  namespace MIDI {

    class managerObject {
    public:
      fileImpl * addImplementation(file * head);
      void update();

    private:
      std::forward_list<fileImpl> implementations;

    };

    managerObject & Manager();
  }
}



#endif  // MIDIFILEMANAGER_H_INCLUDED

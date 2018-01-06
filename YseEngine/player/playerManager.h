/*
  ==============================================================================

    playerManager.h
    Created: 9 Apr 2015 1:39:02pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYERMANAGER_H_INCLUDED
#define PLAYERMANAGER_H_INCLUDED

#include "player.hpp"
#include "playerImplementation.h"
#include <forward_list>

namespace YSE {
  namespace PLAYER {

    class managerObject {
    public:
      managerObject();
      ~managerObject();

      void update(Flt delta);

      //implementationObject * addImplementation(player * head, synth * s);
      void removeImplementation(implementationObject * impl);
      
    private:
      std::forward_list<implementationObject> implementations;
    };

    managerObject & Manager();

  }
}



#endif  // PLAYERMANAGER_H_INCLUDED

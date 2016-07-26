/*
  ==============================================================================

    motifManager.h
    Created: 14 Apr 2015 6:18:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIFMANAGER_H_INCLUDED
#define MOTIFMANAGER_H_INCLUDED

#include "motif.hpp"
#include "motifImplementation.h"
#include <forward_list>

namespace YSE {
  namespace MOTIF {

    class managerObject {
    public:
      managerObject();
      ~managerObject();

      void update();

      implementationObject * addImplementation(motif * head);

    private:
      std::forward_list<implementationObject> implementations;
    };

    managerObject & Manager();

  }
}



#endif  // MOTIFMANAGER_H_INCLUDED

/*
  ==============================================================================

    scaleManager.h
    Created: 14 Apr 2015 2:55:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALEMANAGER_H_INCLUDED
#define SCALEMANAGER_H_INCLUDED

#include "scale.hpp"
#include "scaleImplementation.h"
#include <forward_list>

namespace YSE {
  namespace SCALE {

    class managerObject {
    public:
      managerObject();
      ~managerObject();

      void update();

      implementationObject * addImplementation(scale * head);

    private:
      std::forward_list<implementationObject> implementations;
    };

    managerObject & Manager();

  }
}



#endif  // SCALEMANAGER_H_INCLUDED

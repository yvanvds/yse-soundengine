/*
  ==============================================================================

    interface.hpp
    Created: 10 Mar 2014 9:41:24am
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERFACE_H_INCLUDED
#define INTERFACE_H_INCLUDED

#include "headers/defines.hpp"
#include "headers/types.hpp"
#include "utils/lfQueue.hpp"

namespace YSE {
  /**
   Base class for all public interfaces. 
  */

  template <class IMPLEMENTATION>
  class interface {
  public:
    interface() : pimpl(nullptr) {}

    ~interface() {
      if (pimpl != nullptr) {
        pimpl->head = nullptr;
        pimpl = nullptr;
      }
    }

    virtual interface& create() {
      assert(pimpl != nullptr);
    }



  protected:
    IMPLEMENTATION * pimpl;   
  };
}



#endif  // INTERFACE_H_INCLUDED

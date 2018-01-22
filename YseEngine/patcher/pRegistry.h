#pragma once
#include <map>
#include <string>
#include "pObject.h"


typedef YSE::PATCHER::pObject* (*pObjectFunc)(void);

namespace YSE {
  namespace PATCHER {

    class pRegistry {
    public:
      pRegistry(); // add all objects in the constructor

      pObject* Get(const std::string & objectID);

      bool IsValidObject(const char * objectID);

    private:
      void Add(const std::string & objectID, pObjectFunc);

      std::map<std::string, pObjectFunc> map;
    };

    pRegistry & Register();
  }
}
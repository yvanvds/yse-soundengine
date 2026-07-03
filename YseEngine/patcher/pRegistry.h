#pragma once
#include <map>
#include <string>
#include <vector>
#include "pObject.h"

typedef YSE::PATCHER::pObject* (*pObjectFunc)(void);

namespace YSE {
  namespace PATCHER {

    class pRegistry {
    public:
      pRegistry(); // add all objects in the constructor

      pObject* Get(const std::string& objectID);

      bool IsValidObject(const char* objectID);

      // Returns the type-ID strings of every registered object, in the order
      // they live in the underlying std::map (lexicographic). Used by the
      // test_doc_coverage doctest to iterate the full registry.
      std::vector<std::string> AllNames() const;

    private:
      void Add(const std::string& objectID, pObjectFunc);

      std::map<std::string, pObjectFunc> map;
    };

    pRegistry& Register();
  } // namespace PATCHER
} // namespace YSE
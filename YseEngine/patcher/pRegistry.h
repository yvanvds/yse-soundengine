#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "pObject.h"

typedef YSE::PATCHER::pObject* (*pObjectFunc)();

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

      // True for a registered object that holds a MIDI device port (`.midiout`,
      // the input family, `.sysexin`, `.midiinfo`, ...). Those are registered
      // only when libyse is built with YSE_ENABLE_MIDI_DEVICE — off on macOS
      // and Android — so the metadata surfaces this flag for the docs and for
      // editors that build palettes from one platform's metadata (issue #870).
      // False for unknown names. RT-cold.
      bool RequiresMidiDevice(const std::string& objectID) const;

    private:
      void Add(const std::string& objectID, pObjectFunc);
      // Add() for an object inside the YSE_ENABLE_MIDI_DEVICE guard.
      void AddMidiDevice(const std::string& objectID, pObjectFunc);

      std::map<std::string, pObjectFunc> map;
      std::set<std::string> midiDevice;
    };

    pRegistry& Register();
  } // namespace PATCHER
} // namespace YSE
#pragma once
#include "headers/defines.hpp"
#include <string>
#include "headers\enums.hpp"
#include "utils\vector.hpp"

namespace YSE {
  namespace PATCHER {
    class pObject;
    class patcherImplementation;
  }

  class API pHandle {
  public:
    pHandle(PATCHER::pObject * obj);
    const char * Type() const;

    void SetData(unsigned int inlet, float value);
    void SetParams(const std::string & args);

    void SetPosition(const YSE::Pos & pos);
    YSE::Pos GetPosition();

    int GetInputs();
    int GetOutputs();

    bool IsDSPInput(unsigned int inlet);
    YSE::OUT_TYPE OutputDataType(unsigned int pin);

    // use to get content after loading a JSON Patch
    std::string GetName();
    std::string GetParams();
    unsigned int GetID();
    unsigned int GetConnections(unsigned int outlet);
    unsigned int GetConnectionTarget(unsigned int outlet, unsigned int connection);
    unsigned int GetConnectionTargetInlet(unsigned int outlet, unsigned int connection);

  private:
    PATCHER::pObject * object;
    friend class YSE::PATCHER::patcherImplementation;
  };


}
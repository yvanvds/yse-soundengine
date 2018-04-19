#pragma once
#include "../headers/defines.hpp"
#include <string>
#include "../headers\enums.hpp"
#include "../utils\vector.hpp"

namespace YSE {
  namespace PATCHER {
    class pObject;
    class patcherImplementation;
  }

  class API pHandle {
  public:
    pHandle(PATCHER::pObject * obj);
    const char * Type() const;

    void SetBang(unsigned int inlet);
    void SetIntData(unsigned int inlet, int value);
    void SetFloatData(unsigned int inlet, float value);
    void SetListData(unsigned int inlet, const std::string & value);
    void SetParams(const std::string & args);

    std::string GetGuiProperty(const std::string & key);
    void SetGuiProperty(const std::string & key, const std::string & value);

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

    std::string GetGuiValue();
  private:
    PATCHER::pObject * object;
    friend class YSE::PATCHER::patcherImplementation;
  };


}
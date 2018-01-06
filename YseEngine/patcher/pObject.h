#pragma once
#include <vector>
#include "pin.h"
#include "pEnums.h"
#include "headers\enums.hpp"
#include "headers/defines.hpp"

namespace YSE {
  namespace PATCHER {

    class API pObject {
    public:
      virtual const char * Type() const = 0;
      
      virtual void ResetData();
      virtual void RequestData() = 0;

      bool SetData(unsigned int pin, bool value);
      bool SetData(unsigned int pin, int value);
      bool SetData(unsigned int pin, float value);
      bool SetData(unsigned int pin, const char * value);

      virtual void ConnectInput(pinOut * from, int toPin);
      virtual void DisconnectInput(int pin);

      virtual void ConnectOutput(pinIn * dest, int toPin);
      // outputs will be disconnected from the other side

      inline int NumInputs() const { return inputs.size(); }
      inline int NumOutputs() const { return outputs.size(); }

      PIN_TYPE GetOutputType(unsigned int output) const;
      int GetInputTypes(unsigned int input) const;
      pinIn * GetInput(int pin);
      pinOut * GetOutput(int pin);

    protected:
      void UpdateInputs();

      std::vector<pinIn> inputs;
      std::vector<pinOut> outputs;
    };

  }
}
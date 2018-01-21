#pragma once
#include <vector>
#include "inlet.h"
#include "outlet.h"
#include "pEnums.h"
#include "headers\enums.hpp"
#include "headers/defines.hpp"
#include "dsp/buffer.hpp"
#include "pObjectList.hpp"

namespace YSE {
  namespace PATCHER {

    class API pObject {
    public:
      pObject(bool isDSPObject);

      virtual const char * Type() const = 0;
      virtual void Calculate() = 0;
      virtual void SetParam(unsigned int pos, float value) = 0;
      virtual void SetMessage(const std::string & message, float value) = 0;

      void ConnectInlet(outlet * from, int toPin);
      void DisconnectInlet(outlet * from, int toPin);

      virtual void ConnectOutlet(inlet * dest, int toPin);
      // outputs will be disconnected from the other side

      inline int NumInputs() const { return inputs.size(); }
      inline int NumOutputs() const { return outputs.size(); }

      OUT_TYPE GetOutputType(unsigned int output) const;
      inlet * GetInlet(int number);
      outlet * GetOutlet(int number);

      virtual void ResetDSP();
      void CalculateIfReady();
      bool IsDSPStartPoint();
    protected:

      std::vector<inlet> inputs;
      std::vector<outlet> outputs;
      bool DSP;
    };

  }
}

// these macro's should make creating patcher objects a bit easier
#define PATCHER_CLASS(className, name) class className : public pObject { public: className(); virtual const char * Type() const {return name;} CREATE(className)
#define CREATE(className)  static pObject * Create() { return new className(); }

#define _HAS_PARAMS virtual void SetParam(unsigned int pos, float value);
#define _NO_PARAMS virtual void SetParam(unsigned int pos, float value) {}
#define PARAMS_FUNC() void className::SetParam(unsigned int pos, float value)

#define _HAS_MESSAGES virtual void SetMessage(const std::string & message, float value);
#define _NO_MESSAGES  virtual void SetMessage(const std::string & message, float value) {}
#define MESSAGES_FUNC() void className::SetMessage(const std::string & message, float value)

#define _HAS_CALCULATE virtual void Calculate();
#define _NO_CALCULATE virtual void Calculate() {}
#define CALC_FUNC() void className::Calculate()

#define _HAS_DSP_RESET virtual void ResetDSP();
#define RESET_FUNC() void className::ResetDSP() { pObject::ResetDSP();

#define BUFFER_IN(funcName) void funcName(YSE::DSP::buffer * buffer, int inlet);
#define FLOAT_IN(funcName) void funcName(float value, int inlet);

#define BUFFER_IN_FUNC(funcName) void funcName(YSE::DSP::buffer * buffer, int inlet)
#define FLOAT_IN_FUNC(funcName) void funcName(float value, int inlet)

#define ADD_INLET_0 inputs.emplace_back(this, true, 0)
#define ADD_INLET_1 inputs.emplace_back(this, false, 1)
#define ADD_INLET_2 inputs.emplace_back(this, false, 2)
#define ADD_INLET_3 inputs.emplace_back(this, false, 3)

#define REG_BUFFER_FUNC(funcName) inputs.back().RegisterBuffer(std::bind(&funcName, this, std::placeholders::_1, std::placeholders::_2))
#define REG_FLOAT_FUNC(funcName) inputs.back().RegisterFloat(std::bind(&funcName, this, std::placeholders::_1, std::placeholders::_2))

#define ADD_OUTLET_BUFFER outputs.emplace_back(this, OUT_TYPE::BUFFER)
#define ADD_OUTLET_FLOAT outputs.emplace_back(this, OUT_TYPE::FLOAT)

#define CONSTRUCT_DSP() className::className() : pObject(true)
#define CONSTRUCT() className::className() : pObject(false)
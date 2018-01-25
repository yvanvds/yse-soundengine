#pragma once
#include <vector>
#include "inlet.h"
#include "outlet.h"
#include "pEnums.h"
#include "headers\enums.hpp"
#include "headers/defines.hpp"
#include "dsp/buffer.hpp"
#include "pObjectList.hpp"
#include "utils\json.hpp"
#include "utils\vector.hpp"
#include "parameters.h"
#include "patcher.hpp"

namespace YSE {
  namespace PATCHER {

    typedef std::function<void(int, int)> intCallbackFunc;
    typedef std::function<void(int, float)> floatCallbackFunc;

    class API pObject {
    public:
      pObject(bool isDSPObject);

      virtual const char * Type() const = 0;
      virtual void Calculate() = 0;
      virtual void SetMessage(const std::string & message, float value) = 0;

      void SetParams(const std::string & args);
      const std::string & GetParams();

      void ConnectInlet(outlet * from, int toPin);
      void DisconnectInlet(outlet * from, int toPin);

      virtual void ConnectOutlet(inlet * dest, int toPin);
      // outputs will be disconnected from the other side

      inline int NumInputs() const { return inputs.size(); }
      inline int NumOutputs() const { return outputs.size(); }

      OUT_TYPE GetOutputType(unsigned int output) const;
      inlet * GetInlet(int number);
      outlet * GetOutlet(int number);
      unsigned int GetConnections(unsigned int outlet);
      unsigned int GetConnectionTarget(unsigned int outlet, unsigned int connection);
      unsigned int GetConnectionTargetInlet(unsigned int outlet, unsigned int connection);

      virtual void ResetDSP();
      void CalculateIfReady();
      bool IsDSPStartPoint();

      inline void SetPosition(const YSE::Pos & pos) { this->pos = pos; }
      inline const YSE::Pos & GetPosition() { return pos; }

      void UpdateGui();
      void RegisterGuiHandler(YSE::guiHandler * handler);

      static unsigned int CreateID();
      inline unsigned int GetID() { return ID; }
      void DumpJson(nlohmann::json::value_type & json);

    protected:

      std::vector<inlet> inputs;
      std::vector<outlet> outputs;

      YSE::guiHandler * handler;
      int * guiInt;
      float * guiFlt;

      Parameters parms;

      bool DSP;

      // for display on screen
      YSE::Pos pos;

      // for storage
      int ID;
    };

  }
}

// these macro's should make creating patcher objects a bit easier
#define PATCHER_CLASS(className, name) class className : public pObject { public: className(); virtual const char * Type() const {return name;} CREATE(className)
#define CREATE(className)  static pObject * Create() { return new className(); }

#define _DO_MESSAGES virtual void SetMessage(const std::string & message, float value);
#define _NO_MESSAGES virtual void SetMessage(const std::string & message, float value) {}
#define MESSAGES() void className::SetMessage(const std::string & message, float value)

#define _DO_CALCULATE virtual void Calculate();
#define _NO_CALCULATE virtual void Calculate() {}
#define CALC() void className::Calculate()

#define _DO_RESET virtual void ResetDSP();
#define RESET() void className::ResetDSP() { pObject::ResetDSP();

#define _BUFFER_IN(funcName) void funcName(YSE::DSP::buffer * buffer, int inlet);
#define _FLOAT_IN(funcName) void funcName(float value, int inlet);
#define _INT_IN(funcName) void funcName(int value, int inlet);
#define _BANG_IN(funcName) void funcName(int inlet);

#define BUFFER_IN(funcName) void funcName(YSE::DSP::buffer * buffer, int inlet)
#define FLOAT_IN(funcName) void funcName(float value, int inlet)
#define INT_IN(funcName) void funcName(int value, int inlet)
#define BANG_IN(funcName) void funcName(int inlet)

#define ADD_IN_0 inputs.emplace_back(this, true, 0)
#define ADD_IN_1 inputs.emplace_back(this, false, 1)
#define ADD_IN_2 inputs.emplace_back(this, false, 2)
#define ADD_IN_3 inputs.emplace_back(this, false, 3)

#define REG_BUFFER_IN(funcName) inputs.back().RegisterBuffer(std::bind(&funcName, this, std::placeholders::_1, std::placeholders::_2))
#define REG_FLOAT_IN(funcName) inputs.back().RegisterFloat(std::bind(&funcName, this, std::placeholders::_1, std::placeholders::_2))
#define REG_INT_IN(funcName) inputs.back().RegisterInt(std::bind(&funcName, this, std::placeholders::_1, std::placeholders::_2))
#define REG_BANG_IN(funcName) inputs.back().RegisterBang(std::bind(&funcName, this, std::placeholders::_1))

#define ADD_OUT_BUFFER outputs.emplace_back(this, OUT_TYPE::BUFFER)
#define ADD_OUT_FLOAT outputs.emplace_back(this, OUT_TYPE::FLOAT)
#define ADD_OUT_INT outputs.emplace_back(this, OUT_TYPE::INT)
#define ADD_OUT_BANG outputs.emplace_back(this, OUT_TYPE::BANG)

#define ADD_PARAM(var) parms.Register(var)

#define PASS_GUI_INT(var) guiInt = &var
#define PASS_GUI_FLT(var) guiFlt = &var

#define CONSTRUCT_DSP() className::className() : pObject(true)
#define CONSTRUCT() className::className() : pObject(false)
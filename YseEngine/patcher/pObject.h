#pragma once
#include <vector>
#include <string>
#include <map>
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
      pObject(bool isDSPObject, pObject * parent = nullptr);
	  virtual ~pObject() {}

      virtual const char * Type() const = 0;
      virtual void Calculate(THREAD thread) = 0;
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
      void CalculateIfReady(THREAD thread);
      bool IsDSPStartPoint();
      inline bool IsDSPObject() { return DSP; }

      std::string GetGuiProperty(const std::string & key);
      void SetGuiProperty(const std::string & key, const std::string & value);
      virtual std::string GetGuiValue() { return ""; }
      
      static unsigned int CreateID();
      inline unsigned int GetID() { return ID; }
      void DumpJson(nlohmann::json::value_type & json);

      void SetParent(pObject * parent);
      inline const std::string & DataName() { return dataName; }
    protected:

      std::vector<inlet> inputs;
      std::vector<outlet> outputs;
      std::map<std::string, std::string> guiProperties;

      Parameters parms;
      pObject * parent;
      bool DSP;

      // for storage
      int ID;

      // for incoming data
      std::string dataName;
    };

  }
}

// these macro's should make creating patcher objects a bit easier
#define PATCHER_CLASS(className, name) class className : public pObject { public: className(); virtual const char * Type() const {return name;} CREATE(className)
#define CREATE(className)  static pObject * Create() { return new className(); }

#define _DO_MESSAGES virtual void SetMessage(const std::string & message, float value);
#define _NO_MESSAGES virtual void SetMessage(const std::string & message, float value) {}
#define MESSAGES() void className::SetMessage(const std::string & message, float value)

#define _DO_CALCULATE virtual void Calculate(YSE::THREAD thread);
#define _NO_CALCULATE virtual void Calculate(YSE::THREAD thread) {}
#define CALC() void className::Calculate(YSE::THREAD thread)

#define _DO_RESET virtual void ResetDSP();
#define RESET() void className::ResetDSP() { pObject::ResetDSP();

#define _BUFFER_IN(funcName) void funcName(YSE::DSP::buffer * buffer, int inlet, YSE::THREAD thread);
#define _FLOAT_IN(funcName) void funcName(float value, int inlet, YSE::THREAD thread);
#define _INT_IN(funcName) void funcName(int value, int inlet, YSE::THREAD thread);
#define _BANG_IN(funcName) void funcName(int inlet, YSE::THREAD thread);
#define _LIST_IN(funcName) void funcName(const std::string & value, int inlet, YSE::THREAD thread);

#define BUFFER_IN(funcName) void className::funcName(YSE::DSP::buffer * buffer, int inlet, YSE::THREAD thread)
#define FLOAT_IN(funcName) void className::funcName(float value, int inlet, YSE::THREAD thread)
#define INT_IN(funcName) void className::funcName(int value, int inlet, YSE::THREAD thread)
#define BANG_IN(funcName) void className::funcName(int inlet, YSE::THREAD thread)
#define LIST_IN(funcName) void className::funcName(const std::string & value, int inlet, YSE::THREAD thread)

#define ADD_IN_0 inputs.emplace_back(this, true, 0)
#define ADD_IN_1 inputs.emplace_back(this, false, 1)
#define ADD_IN_2 inputs.emplace_back(this, false, 2)
#define ADD_IN_3 inputs.emplace_back(this, false, 3)

#define REG_BUFFER_IN(funcName) inputs.back().RegisterBuffer(std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
#define REG_FLOAT_IN(funcName) inputs.back().RegisterFloat(std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
#define REG_INT_IN(funcName) inputs.back().RegisterInt(std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
#define REG_LIST_IN(funcName) inputs.back().RegisterList(std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
#define REG_BANG_IN(funcName) inputs.back().RegisterBang(std::bind(&className::funcName, this, std::placeholders::_1, std::placeholders::_2))


#define ADD_OUT_BUFFER outputs.emplace_back(OUT_TYPE::BUFFER)
#define ADD_OUT_FLOAT outputs.emplace_back(OUT_TYPE::FLOAT)
#define ADD_OUT_INT outputs.emplace_back(OUT_TYPE::INT)
#define ADD_OUT_BANG outputs.emplace_back(OUT_TYPE::BANG)
#define ADD_OUT_LIST outputs.emplace_back(OUT_TYPE::LIST)
#define ADD_OUT_ANY outputs.emplace_back(OUT_TYPE::ANY)

#define ADD_PARAM(var) parms.Register(var)

#define _HAS_GUI virtual std::string GetGuiValue();
#define GUI_VALUE() std::string className::GetGuiValue()

#define CONSTRUCT_DSP() className::className() : pObject(true)
#define CONSTRUCT() className::className() : pObject(false)

#define _PARM_CLEAR void ClearParams();
#define _PARM_PARSE void ParseParams();
#define PARM_CLEAR() void className::ClearParams()
#define PARM_PARSE() void className::ParseParams()
#define REG_PARM_CLEAR parms.RegisterClear(std::bind(&className::ClearParams, this))
#define REG_PARM_PARSE parms.RegisterParse(std::bind(&className::ParseParams, this))


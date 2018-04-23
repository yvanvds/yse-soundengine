#pragma once
#include <vector>
#include <string>
#include <atomic>
#include <functional>

namespace YSE {
  namespace PATCHER {

    enum PARM_TYPE {
      FLOAT,
      ATOMIC_FLOAT,
      INT,
      ATOMIC_INT,
      STRING,
      LIST,
    };

    struct parameter {
      parameter(PARM_TYPE type, void * value) 
        : value(value), type(type)
      {}
      void * value;
      PARM_TYPE type;
    };

    typedef std::function<void()> parmFunc;

    class Parameters {
    public:
      Parameters() : onClear(nullptr), onParse(nullptr) {}
      void Register(int & value);
      void Register(std::atomic<int> & value);
      void Register(float & value);
      void Register(std::atomic<float> & value);
      void Register(std::string & value);
      void Register(std::vector<std::string> & list);

      void Set(const std::string & args);

      void RegisterClear(parmFunc f);
      void RegisterParse(parmFunc f);

      const std::string & Get();
    private:
      std::vector<parameter> parms;
      std::string current;
      parmFunc onClear;
      parmFunc onParse;
    };

  }
}
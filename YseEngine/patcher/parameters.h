#pragma once
#include <vector>
#include <string>
#include <atomic>

namespace YSE {
  namespace PATCHER {

    enum PARM_TYPE {
      FLOAT,
      ATOMIC_FLOAT,
      INT,
      ATOMIC_INT,
      STRING,
    };

    struct parameter {
      parameter(PARM_TYPE type, void * value) : type(type), value(value) {}
      void * value;
      PARM_TYPE type;
    };

    class Parameters {
    public:
      void Register(int & value);
      void Register(std::atomic<int> & value);
      void Register(float & value);
      void Register(std::atomic<float> & value);
      void Register(std::string & value);

      void Set(const std::string & args);
      const std::string & Get();
    private:
      std::vector<parameter> parms;
      std::string current;
    };

  }
}
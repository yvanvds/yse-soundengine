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

    // Per-parameter documentation entry. Populated via PARAM_DOC in object
    // constructors; the order is expected to match the Register() call order
    // for the corresponding object.
    struct ParamDoc {
      std::string name;
      std::string defaultValue;
      std::string doc;
      std::string range;
    };

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

      // Documentation surface. SetDoc() appends a ParamDoc entry; call once
      // per ADD_PARAM in the same order. RT-cold: only called at construction.
      void SetDoc(const std::string & name,
                  const std::string & defaultValue,
                  const std::string & doc,
                  const std::string & range);
      const std::vector<ParamDoc> & GetDocs() const { return docs; }
      std::size_t Count() const { return parms.size(); }
    private:
      std::vector<parameter> parms;
      std::string current;
      parmFunc onClear;
      parmFunc onParse;
      std::vector<ParamDoc> docs;
    };

  }
}
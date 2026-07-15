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
      parameter(PARM_TYPE type, void* value) : value(value), type(type) {}
      void* value;
      PARM_TYPE type;
    };

    typedef std::function<void()> parmFunc;

    // One pre-parsed scalar write of a live SetParams re-parse (issue #234).
    // Built on the control thread by Parameters::BuildPlan and applied with a
    // plain/atomic store on the audio thread at the top of the next block, so
    // the live field is never written concurrently with the render that reads
    // it. POD on purpose: plans ride a bounded lock-free queue by value.
    struct ParamOp {
      PARM_TYPE type;
      void* dest;
      int i;
      float f;
    };

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
      void Register(int& value);
      void Register(std::atomic<int>& value);
      void Register(float& value);
      void Register(std::atomic<float>& value);
      void Register(std::string& value);
      void Register(std::vector<std::string>& list);

      void Set(const std::string& args);

      // Whether a live re-parse must rebuild the object instead of patching
      // scalar fields (issue #234): true when the object registered
      // clear/parse callbacks (they translate params into pin structure) or
      // any STRING/LIST param (strings cannot be written allocation-free and
      // are read on both threads once the object is published).
      bool NeedsRebuild() const;

      // Tokenize `args` exactly like Set(), but record the scalar writes into
      // `ops` instead of touching the live fields, and update the stored
      // param string. Returns the number of ops written, 0 for empty args, or
      // -1 when the plan does not fit `cap` (or a non-scalar param sneaks in)
      // — the caller must then fall back to the structural rebuild. Parse
      // errors (std::stof on garbage) throw here, on the calling thread,
      // exactly as Set() does. Only meaningful when NeedsRebuild() is false.
      int BuildPlan(const std::string& args, ParamOp* ops, int cap);

      void RegisterClear(parmFunc f);
      void RegisterParse(parmFunc f);

      const std::string& Get();

      // Documentation surface. SetDoc() appends a ParamDoc entry; call once
      // per ADD_PARAM in the same order. RT-cold: only called at construction.
      void SetDoc(const std::string& name, const std::string& defaultValue, const std::string& doc,
                  const std::string& range);
      const std::vector<ParamDoc>& GetDocs() const {
        return docs;
      }
      std::size_t Count() const {
        return parms.size();
      }

    private:
      std::vector<parameter> parms;
      std::string current;
      parmFunc onClear;
      parmFunc onParse;
      std::vector<ParamDoc> docs;
    };

  } // namespace PATCHER
} // namespace YSE
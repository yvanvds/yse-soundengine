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
    // Built on the control thread by Parameters::BuildPlanFrom and applied with a
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

      // Parse `args` into the registered parameters, left to right, one
      // whitespace-separated token each. Tokens are split on runs of spaces,
      // tabs and line breaks, like list messages, so "0  10" and "0\t10" mean
      // "0 10" and a leading or trailing run is ignored; arguments that are
      // only whitespace parse like "" (issue #936).
      //
      // Surplus arguments are *ignored*, not rejected: an object handed more
      // tokens than it has parameters keeps the ones it understands and logs
      // the rest at E_DEBUG — except when the last parameter is a LIST, which
      // absorbs the remainder. An object that registers no parameters at all
      // is the degenerate case of that rule, so every token is ignored (issue
      // #627); it is not an error, and it never throws.
      //
      // The argument string is stored verbatim either way, so Get() / DumpJSON
      // hand back what the object was given rather than a rewritten subset —
      // loading and re-saving a patch file must not quietly drop an argument a
      // newer or older version of the object would use. BuildPlanFrom()
      // carries the same result to a live object on the parented path.
      void Set(const std::string& args);

      // Whether a live re-parse must rebuild the object instead of patching
      // scalar fields (issue #234): true when the object registered
      // clear/parse callbacks (they translate params into pin structure) or
      // any STRING/LIST param (strings cannot be written allocation-free and
      // are read on both threads once the object is published).
      bool NeedsRebuild() const;

      // Plan a live re-parse (issue #234) as the object a rebuild would give
      // (issue #935). `staged` is the parameter set of a fresh object of the
      // same type that has just run Set(args): every registered parameter —
      // the ones `args` named and the ones it left out, which hold their
      // defaults there — becomes one op in `ops` writing the staged value to
      // this object's field, and the stored string becomes `args`. So a live
      // object, a rebuilt one and one reloaded from DumpJSON always agree, and
      // "" resets every parameter, exactly as it does on a rebuild.
      //
      // The live fields are not touched: the ops are applied later with plain
      // stores on the audio thread. Returns the number of ops written (0 for
      // an object without parameters), or -1 when they do not fit `cap` or a
      // non-scalar param sneaks in — the caller must then fall back to the
      // structural rebuild. Only meaningful when NeedsRebuild() is false.
      int BuildPlanFrom(const Parameters& staged, ParamOp* ops, int cap);

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
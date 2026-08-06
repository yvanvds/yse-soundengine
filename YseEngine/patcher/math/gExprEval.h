#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The expression compiler and evaluator shared by ``.expr`` (#449)
     *         and ``.vexpr`` (#450).
     *
     *  A small C-like expression language, modelled on Max's ``expr``. The
     *  split of work is the whole point of the class:
     *
     *  - ExprProgram::Compile() runs on the **control thread only**. It
     *    tokenizes, parses with a recursive-descent parser and lowers the
     *    result into a flat postfix program (``std::vector<ExprInstr>``). It
     *    allocates, it reports errors as strings, and it is never called from
     *    a patcher object outside ``SetParams`` / construction.
     *  - ExprProgram::Evaluate() is the **audio/control path**. It walks the
     *    already-built instruction array over a fixed-size stack living in the
     *    caller's frame. No allocation, no locking, no exceptions, no I/O, no
     *    recursion, no libm call that is not already in the DSP budget.
     *
     *  ### Language
     *
     *  Values are typed ``int`` or ``float``, as in C, and the type is carried
     *  on the evaluation stack rather than inferred by the caller:
     *
     *  - ``$i1``-``$i9`` read inlet 1-9 as an int (the stored float is
     *    truncated towards zero), ``$f1``-``$f9`` read the same inlet as a
     *    float. A literal without a ``.`` or an exponent is an int.
     *  - ``+ - *`` and unary ``-`` give an int when both operands are ints,
     *    otherwise a float. So does ``/``, which means ``$i1/$i2`` is C's
     *    integer division — the one result that surprises people, and the one
     *    Max gives too.
     *  - ``%`` is the integer remainder on two ints and ``fmod`` otherwise.
     *  - Comparisons (``< <= > >= == !=``), the logical operators
     *    (``&& || !``) and the bitwise operators (``& | ^ << >> ~``) always
     *    give an int. ``^`` is bitwise exclusive-or, **not** exponentiation —
     *    use ``pow()``. That is Max's reading of the operator as well.
     *  - Functions: ``int`` and ``float`` convert; ``abs``, ``min`` and
     *    ``max`` keep the int type when every argument is an int; every other
     *    function (``sqrt exp ln log log10 sin cos tan asin acos atan atan2
     *    sinh cosh tanh pow round floor ceil fact``) returns a float.
     *
     *  Division by zero yields 0 rather than an infinity or a trap, matching
     *  ``./``, ``.%`` and ``.div``; ``INT_MIN / -1`` yields ``INT_MIN`` rather
     *  than trapping, matching ``.div``; a shift count outside 0-31 is
     *  answered rather than left undefined, matching ``.<<`` / ``.>>``. A
     *  float result that is not finite is replaced by 0 before it is returned,
     *  the convention the whole math family uses.
     *
     *  Not supported, and rejected at compile time with a message that says
     *  so: Max's table access (``$s1``, ``$s2[7]``) and the table/stochastic
     *  functions (``size sum Sum avg Avg store random noise``). The YSE
     *  patcher has no ``table`` object, and randomness has its own object
     *  (``.random``) — keeping the evaluator pure also keeps it trivially
     *  reusable for ``.vexpr``, which runs it once per list element.
     */

    /// Max caps ``expr``/``vexpr`` at nine changeable arguments.
    constexpr int kExprMaxVars = 9;

    /// Depth of Evaluate()'s value stack. Compile() refuses any program that
    /// would need more, so the walk can never run off the end of the array.
    constexpr int kExprMaxStack = 32;

    /// Recursive-descent nesting cap, so a pathological ``((((((...`` cannot
    /// overflow the *compiler's* own C++ stack either.
    constexpr int kExprMaxDepth = 32;

    /// Opcodes of the flat postfix program Compile() emits.
    enum class ExprOp : unsigned char {
      // Operands (stack effect +1).
      PUSH_INT, ///< push ExprInstr::i as an int
      PUSH_FLOAT, ///< push ExprInstr::f as a float
      PUSH_VAR_INT, ///< push vars[ExprInstr::i] truncated to an int ($iN)
      PUSH_VAR_FLOAT, ///< push vars[ExprInstr::i] as a float ($fN)

      // Unary operators (stack effect 0).
      NEG,
      NOT,
      BITNOT,

      // Binary operators (stack effect -1).
      ADD,
      SUB,
      MUL,
      DIV,
      MOD,
      SHL,
      SHR,
      LT,
      LE,
      GT,
      GE,
      EQ,
      NE,
      BITAND,
      BITXOR,
      BITOR,
      AND,
      OR,

      // One-argument functions (stack effect 0).
      F_ABS,
      F_INT,
      F_FLOAT,
      F_SQRT,
      F_EXP,
      F_LN,
      F_LOG10,
      F_SIN,
      F_COS,
      F_TAN,
      F_ASIN,
      F_ACOS,
      F_ATAN,
      F_SINH,
      F_COSH,
      F_TANH,
      F_ROUND,
      F_FLOOR,
      F_CEIL,
      F_FACT,

      // Two-argument functions (stack effect -1).
      F_MIN,
      F_MAX,
      F_POW,
      F_ATAN2,
    };

    /// One instruction of the compiled program. POD, so the whole program is a
    /// contiguous array Evaluate() streams through.
    struct ExprInstr {
      ExprOp op = ExprOp::PUSH_INT;
      int i = 0; ///< PUSH_INT literal, or the 0-based variable slot
      float f = 0.f; ///< PUSH_FLOAT literal
    };

    /// Truncate a float to an int the way C would, but answer the inputs C
    /// leaves undefined (NaN, an infinity, anything outside the int range)
    /// with 0 instead of whatever the hardware happens to do. Branch-only.
    inline int ExprToInt(float v) {
      // Written as a negated range test so a NaN — which compares false
      // against everything — takes the 0 branch as well.
      if (!(v >= -2147483648.f && v < 2147483648.f)) return 0;
      return (int)v;
    }

    /// A value on the evaluation stack: a number plus the C type it carries.
    struct ExprValue {
      float f = 0.f;
      int i = 0;
      bool isInt = true;

      static ExprValue Int(int v) {
        ExprValue r;
        r.isInt = true;
        r.i = v;
        return r;
      }
      static ExprValue Float(float v) {
        ExprValue r;
        r.isInt = false;
        r.f = v;
        return r;
      }

      int AsInt() const {
        return isInt ? i : ExprToInt(f);
      }
      float AsFloat() const {
        return isInt ? (float)i : f;
      }
    };

    /**
     *  @brief A compiled expression: build it once on the control thread, walk
     *         it as often as you like on the audio thread.
     */
    class ExprProgram {
    public:
      /**
       *  Compile @p source. Control thread only — allocates, and builds an
       *  error string on failure.
       *
       *  @return true when the program is usable. On failure the program is
       *          emptied (Evaluate() then returns int 0), Error() describes
       *          what went wrong and where, and the caller is expected to log
       *          it: a malformed expression must fail here, loudly, rather
       *          than silently at render time.
       */
      bool Compile(const std::string& source);

      /// Drop the program and the error. Control thread only.
      void Clear();

      /// True when a successful Compile() produced a runnable program.
      bool Valid() const {
        return valid;
      }

      /// Empty on success; on failure, "what went wrong" plus a character
      /// offset into the source.
      const std::string& Error() const {
        return error;
      }

      /// Number of inlets the expression needs: one past the highest ``$``
      /// index it references, and 1 when it references none — Max always
      /// gives ``expr`` a left inlet to bang.
      int InletCount() const {
        return inletCount;
      }

      /// Whether the expression actually reads inlet @p index (0-based).
      /// Inlets between two referenced ones exist but are never read, exactly
      /// as in Max.
      bool UsesInlet(int index) const {
        if (index < 0 || index >= kExprMaxVars) return false;
        return (usedInlets & (1u << index)) != 0;
      }

      /// Instruction count; 0 for an empty or failed program.
      std::size_t Size() const {
        return code.size();
      }

      /// Deepest the value stack gets while walking this program.
      int StackDepth() const {
        return stackNeeded;
      }

      /**
       *  Walk the compiled program. RT-safe: a fixed-size stack in this
       *  frame, a switch per instruction, no allocation, no lock, no
       *  exception, no I/O, no recursion.
       *
       *  @param vars  kExprMaxVars floats, the per-inlet stored values. Only
       *               the slots the expression references are read.
       *  @return the result, tagged int or float. An empty program, and any
       *          float result that is not finite, gives 0.
       */
      ExprValue Evaluate(const float* vars) const;

    private:
      std::vector<ExprInstr> code;
      std::string error;
      unsigned int usedInlets = 0;
      int inletCount = 1;
      int stackNeeded = 0;
      bool valid = false;
    };

    /**
     *  Parse a space-separated list of numbers into @p out, at most @p cap of
     *  them. RT-safe: walks the characters with ``strtof``, never allocates,
     *  never throws — a list arriving on the audio thread must not do either.
     *  Tokens that are not numbers are skipped rather than rejected.
     *
     *  Shared because ``.vexpr`` (#450) reads its whole input as a list.
     *
     *  @return how many numbers were written.
     */
    int ExprParseFloatList(const char* text, float* out, int cap);

  } // namespace PATCHER
} // namespace YSE

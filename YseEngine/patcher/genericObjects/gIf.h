#pragma once
#include "../pObject.h"
#include "../math/gExprEval.h"

namespace YSE {
  namespace PATCHER {

    /// Most items a ``then`` / ``else`` message may hold. Max puts no limit on
    /// the length of a message expression, but the whole point of this object
    /// is that Calculate() never allocates: the outgoing list is written into a
    /// buffer reserved at construction, so the bound has to exist somewhere and
    /// it is cheaper to state it than to grow a string on the audio thread.
    /// Sixteen is well past the point where a message box is the readable
    /// choice, and a longer one is rejected when the statement is compiled.
    constexpr int kIfMaxMessageItems = 16;

    /**
     *  @brief Conditional message dispatch — ``.if`` (issue #451).
     *
     *  Max's ``if``: one object in place of a comparison box plus a gate plus a
     *  select. The creation argument reads as a sentence,
     *
     *      .if $i1 > 64 then bang else out2 $i1
     *
     *  and means what it says — evaluate the condition, and send the ``then``
     *  message when it is non-zero or the ``else`` message when it is not. With
     *  no ``else`` a false condition simply sends nothing, which is the routing
     *  half of the object: it is how a stream gets filtered rather than mapped.
     *
     *  ### The pieces
     *
     *  - The **condition** is everything before the word ``then``, and it is
     *    an ``.expr`` expression — the same language, the same ``$i1``-``$i9``
     *    / ``$f1``-``$f9`` placeholders, the same C type rules and the same
     *    answers for the cases C leaves undefined. See gExprEval.h; ``.if``
     *    adds no syntax of its own and shares ExprProgram with ``.expr`` (#449)
     *    and ``.vexpr`` (#450) rather than carrying a second parser.
     *  - A **message** is a space-separated list of items, each of which is
     *    itself an expression: a literal (``1``, ``0.5``), a placeholder
     *    (``$i1``), or any space-free expression (``$i1*2``). One item is sent
     *    as an int or a float following its C type; two or more are sent as a
     *    list. The word ``bang`` on its own sends a bang.
     *  - The keyword **``out2``** in front of a message routes it to a second,
     *    right outlet, which the object grows only when a message asks for it.
     *
     *  The number of inlets is one past the highest placeholder used *anywhere*
     *  in the statement — condition and both messages — capped at Max's nine.
     *  Inlet 0 is hot, exactly as everywhere else in the family: the cold
     *  inlets store, inlet 0 stores *and* evaluates. A bang on inlet 0
     *  re-evaluates from the stored values and a list fills the inlets left to
     *  right, as in ``.expr``.
     *
     *  ### What is not supported
     *
     *  Max's ``send`` keyword (dispatch to a remote ``receive`` by name) and
     *  its ``$s`` symbol placeholders are rejected when the statement is
     *  compiled, with a message that says which and why: symbols would need the
     *  ``table`` object the YSE patcher does not have, and a ``send`` inside
     *  the object would mean a name lookup on the audio thread when wiring the
     *  outlet to a ``.s`` object costs nothing and is visible in the patch.
     *
     *  ### The RT contract
     *
     *  The same split as ``.expr``. The whole statement is parsed and compiled
     *  in ParseParams — the control thread, at construction or on
     *  ``SetParams`` — into one ExprProgram for the condition and one per
     *  message item. Calculate() only walks those programs over a stack in its
     *  own frame and appends the digits to a buffer reserved at construction:
     *  no parsing, no allocation, no lock, no exception, no I/O. A ``SetParams``
     *  on a live object cannot mutate it in place either — the registered
     *  clear/parse callbacks make ParamsNeedRebuild() true, so the patcher
     *  takes the structural-replacement route from issue #234.
     *
     *  A malformed statement fails at parse time and loudly: the error goes to
     *  the log at E_ERROR, the object is left inert (one inlet, one outlet, no
     *  programs) and it sends nothing at all — deliberately not "the else
     *  branch", which would be a plausible-looking wrong answer.
     */
    PATCHER_CLASS(gIf, YSE::OBJ::G_IF)
    _NO_MESSAGES
    _DO_CALCULATE

    _FLOAT_IN(SetFloat)
    _INT_IN(SetInt)
    _BANG_IN(SetBang)
    _LIST_IN(SetList)

    _PARM_CLEAR
    _PARM_PARSE

  public:
    /// Empty when the current statement compiled; otherwise the parse error
    /// that was logged. Control thread only.
    const std::string& CompileError() const {
      return error;
    }
    /// True when a statement compiled and the object will send something.
    bool Valid() const {
      return valid;
    }
    /// The compiled condition, for tests and for anything that wants to know
    /// which inlets it reads.
    const ExprProgram& Condition() const {
      return condition;
    }
    /// Whether the statement carries an ``else`` message.
    bool HasElse() const {
      return elseMsg.present;
    }
    /// Whether inlet @p index (0-based) is read by the condition or by either
    /// message. Inlets between two referenced ones exist but are never read,
    /// exactly as in Max.
    bool UsesInlet(int index) const {
      if (index < 0 || index >= kExprMaxVars) return false;
      return (usedInlets & (1u << index)) != 0;
    }

  private:
    /// One compiled ``then`` / ``else`` clause.
    struct Message {
      bool present = false; ///< the statement carries this clause at all
      bool isBang = false; ///< the clause is the single word "bang"
      bool toRight = false; ///< the clause was prefixed with "out2"
      int count = 0; ///< live entries in items
      ExprProgram items[kIfMaxMessageItems];

      /// Control thread only: back to the empty clause.
      void Reset() {
        present = false;
        isBang = false;
        toRight = false;
        for (int i = 0; i < count; i++) {
          items[i].Clear();
        }
        count = 0;
      }
    };

    // Record the first parse error and answer false, so the callers can read
    // as a chain of `if (!step()) return false;`.
    bool Fail(const std::string& message);

    // Control thread only. Splits the statement at `then` / `else`, compiles
    // the three pieces and leaves `error` set on failure.
    bool CompileStatement(const std::vector<std::string>& tokens);
    bool CompileMessage(const std::vector<std::string>& tokens, std::size_t from, std::size_t to,
                        const char* which, Message& out);

    // RT path: evaluate the clause's items and send them.
    void Emit(const Message& message, YSE::THREAD thread);

    // Shared by the float and int inlet handlers: every inlet stores into the
    // same float slot, and $iN decides at evaluation time whether to truncate.
    void Store(float value, int inlet);

    // The raw creation argument, one token per space. Registered as a LIST
    // param so a whitespace-bearing statement survives Parameters::Set intact.
    std::vector<std::string> statement;

    ExprProgram condition;
    Message thenMsg;
    Message elseMsg;

    std::string error;
    bool valid = false;

    // Union of the inlets the condition and both messages read; drives the
    // per-inlet documentation.
    unsigned int usedInlets = 0;

    // Per-inlet stored values. Written by the inlet handlers, read by
    // Calculate; sized for the nine inlets Max allows so a program compiled for
    // fewer can never index past the end.
    float vars[kExprMaxVars] = {};

    // The outgoing list, reserved at construction for the longest one that can
    // be built, so appending to it on the hot path cannot reallocate.
    std::string result;
  };
} // namespace PATCHER
} // namespace YSE

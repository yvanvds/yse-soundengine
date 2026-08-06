// Tests for the expression object (issue #449): .expr, and the compiler /
// evaluator it shares with .vexpr (#450) in patcher/math/gExprEval.h.
//
// The file is in two halves.
//
// The first half drives ExprProgram directly, because that is where the risk
// is. A parser is the one patcher component that can fail in a hundred ways
// rather than three, and the RT contract for the object is entirely a
// statement about the split between the two halves of the class: Compile()
// does all the work and may fail; Evaluate() only walks the result and cannot.
// So the parser tests cover the token level (numbers, $-variables, two-
// character operators), the grammar level (C's full precedence ladder,
// associativity, calls), the type level (C's int/float rules, including the
// integer division that surprises people) and — at length — malformed input:
// every rejection has a named test, because "fails loudly at parse time" is an
// acceptance criterion of the issue, not a nicety.
//
// The second half drives the object: inlets grown from the placeholders in the
// expression, the hot/cold split, bang and list on inlet 0, the int-or-float
// outlet, the params round trip and the doc metadata.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/math/gExpr.h"
#include "patcher/math/gExprEval.h"
#include "patcher/sinks.hpp"

using TestHelpers::MultiSink;
using YSE::PATCHER::ExprProgram;
using YSE::PATCHER::ExprValue;
using YSE::PATCHER::kExprMaxVars;

namespace {

  // Compile or die: for the (many) cases where the expression is expected to
  // be well formed and the test is about what it evaluates to.
  ExprProgram Compiled(const std::string& source) {
    ExprProgram p;
    const bool ok = p.Compile(source);
    INFO("compiling: " << source << " -> " << p.Error());
    REQUIRE(ok);
    return p;
  }

  // Evaluate `source` with the given inlet values; slots not given are 0.
  ExprValue Eval(const std::string& source, std::vector<float> inlets = {}) {
    float vars[kExprMaxVars] = {};
    for (std::size_t i = 0; i < inlets.size() && i < (std::size_t)kExprMaxVars; i++) {
      vars[i] = inlets[i];
    }
    return Compiled(source).Evaluate(vars);
  }

  float EvalF(const std::string& source, std::vector<float> inlets = {}) {
    return Eval(source, std::move(inlets)).AsFloat();
  }

  // The compile is expected to fail; returns the error so a test can assert on
  // what it says as well as that it happened.
  std::string CompileError(const std::string& source) {
    ExprProgram p;
    const bool ok = p.Compile(source);
    CHECK_FALSE(ok);
    CHECK_FALSE(p.Valid());
    // A rejected program must be inert, not half-built: evaluating it gives 0
    // rather than walking whatever the parser managed to emit.
    float vars[kExprMaxVars] = {};
    CHECK(p.Size() == 0);
    CHECK(p.Evaluate(vars).AsFloat() == 0.f);
    return p.Error();
  }

  // Holds a .expr and its sink together. The expression is set *before* the
  // outlet is wired, mirroring the patcher's own order (ReplaceObjectUnlocked
  // parses params on an object that is not yet published).
  struct ExprRig {
    YSE::PATCHER::gExpr op;
    MultiSink sink;

    explicit ExprRig(const std::string& expression) {
      op.SetParams(expression);
      op.ConnectOutlet(sink.GetInlet(0), 0);
      sink.ConnectInlet(op.GetOutlet(0), 0);
    }

    void Reset() {
      sink.gotInt = false;
      sink.gotFloat = false;
      sink.gotList = false;
      sink.gotBang = false;
      sink.intValue = 0;
      sink.floatValue = 0.f;
    }

    // Cold inlets only store; use before Feed().
    void Set(int inlet, float value) {
      op.GetInlet(inlet)->SetFloat(value, YSE::T_GUI);
    }

    // Fires the hot inlet after clearing the sink, so the flags describe this
    // evaluation alone.
    void Feed(float value) {
      Reset();
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }

    void Bang() {
      Reset();
      op.GetInlet(0)->SetBang(YSE::T_GUI);
    }

    void FeedList(const std::string& list) {
      Reset();
      op.GetInlet(0)->SetList(list, YSE::T_GUI);
    }

    // Whatever came out, as a float — the outlet is typed ANY and the result
    // carries the expression's C type.
    float Out() const {
      return sink.gotInt ? (float)sink.intValue : sink.floatValue;
    }
  };

  const float kInf = std::numeric_limits<float>::infinity();
  const float kNaN = std::numeric_limits<float>::quiet_NaN();

} // namespace

TEST_SUITE("patcher") {

  // ═══ the evaluator ════════════════════════════════════════════════════════

  // ─── literals and variables ─────────────────────────────────────────────

  TEST_CASE("expr: integer and float literals keep their C type (#449)") {
    CHECK(Eval("42").isInt);
    CHECK(Eval("42").AsInt() == 42);

    CHECK_FALSE(Eval("42.").isInt);
    CHECK(Eval("42.").AsFloat() == doctest::Approx(42.f));

    CHECK_FALSE(Eval("0.5").isInt);
    CHECK(Eval("0.5").AsFloat() == doctest::Approx(0.5f));

    // A leading '.' is a float, and so is anything with an exponent.
    CHECK(Eval(".25").AsFloat() == doctest::Approx(0.25f));
    CHECK_FALSE(Eval("1e3").isInt);
    CHECK(Eval("1e3").AsFloat() == doctest::Approx(1000.f));
    CHECK(Eval("2.5e-2").AsFloat() == doctest::Approx(0.025f));
  }

  // An integer literal too large for an int keeps its value rather than
  // wrapping — C promotes it too.
  TEST_CASE("expr: an oversized integer literal becomes a float (#449)") {
    const ExprValue v = Eval("9999999999");
    CHECK_FALSE(v.isInt);
    CHECK(v.AsFloat() == doctest::Approx(9999999999.f));
  }

  TEST_CASE("expr: $f reads an inlet as a float and $i truncates it (#449)") {
    CHECK(Eval("$f1", {2.7f}).AsFloat() == doctest::Approx(2.7f));
    CHECK_FALSE(Eval("$f1", {2.7f}).isInt);

    CHECK(Eval("$i1", {2.7f}).isInt);
    CHECK(Eval("$i1", {2.7f}).AsInt() == 2);
    // Truncation is towards zero, as in C — not a floor.
    CHECK(Eval("$i1", {-2.7f}).AsInt() == -2);
  }

  TEST_CASE("expr: $i and $f can name the same inlet (#449)") {
    // Max: "the number in the second inlet will be stored in place of the $i2
    // and $f2 arguments, wherever they appear."
    const ExprProgram p = Compiled("$f2 - $i2");
    CHECK(p.InletCount() == 2);
    float vars[kExprMaxVars] = {0.f, 3.75f};
    CHECK(p.Evaluate(vars).AsFloat() == doctest::Approx(0.75f));
  }

  TEST_CASE("expr: an unreferenced inlet reads as 0 (#449)") {
    // "If a value has never been received for each changeable argument, that
    // value is considered 0 when the expression is evaluated."
    CHECK(EvalF("$f1 + $f2 + $f3") == doctest::Approx(0.f));
    CHECK(EvalF("$f1 + $f3", {5.f}) == doctest::Approx(5.f));
  }

  TEST_CASE("expr: the inlet count is one past the highest placeholder (#449)") {
    CHECK(Compiled("1 + 2").InletCount() == 1); // never fewer than one
    CHECK(Compiled("$f1").InletCount() == 1);
    CHECK(Compiled("$i3").InletCount() == 3); // the gap inlets still exist
    CHECK(Compiled("$f9 * $i2").InletCount() == 9);

    const ExprProgram p = Compiled("$f1 + $f3");
    CHECK(p.UsesInlet(0));
    CHECK_FALSE(p.UsesInlet(1)); // exists, never read
    CHECK(p.UsesInlet(2));
    CHECK_FALSE(p.UsesInlet(3));
  }

  // ─── precedence and associativity ───────────────────────────────────────

  TEST_CASE("expr: multiplication binds tighter than addition (#449)") {
    CHECK(EvalF("2 + 3 * 4") == doctest::Approx(14.f));
    CHECK(EvalF("(2 + 3) * 4") == doctest::Approx(20.f));
    CHECK(EvalF("2 * 3 + 4 * 5") == doctest::Approx(26.f));
  }

  TEST_CASE("expr: + - * / are left-associative (#449)") {
    CHECK(EvalF("10 - 3 - 2") == doctest::Approx(5.f)); // not 9
    CHECK(EvalF("100. / 10. / 2.") == doctest::Approx(5.f)); // not 20
    CHECK(EvalF("2 - 3 + 4") == doctest::Approx(3.f));
  }

  // C's ladder, top to bottom. Each case is the one that would come out wrong
  // if the two levels were swapped.
  TEST_CASE("expr: the full C precedence ladder is honoured (#449)") {
    CHECK(EvalF("1 + 2 << 3") == doctest::Approx(24.f)); // (1+2) << 3, not 1 + (2<<3)
    CHECK(EvalF("1 << 3 > 4") == doctest::Approx(1.f)); // (1<<3) > 4, not 1 << (3>4)
    CHECK(EvalF("1 < 2 == 1") == doctest::Approx(1.f)); // (1<2) == 1, not 1 < (2==1)
    CHECK(EvalF("2 == 2 & 1") == doctest::Approx(1.f)); // (2==2) & 1, not 2 == (2&1)
    CHECK(EvalF("1 & 3 ^ 2") == doctest::Approx(3.f)); // (1&3) ^ 2, not 1 & (3^2)
    CHECK(EvalF("7 ^ 3 | 1") == doctest::Approx(5.f)); // (7^3) | 1, not 7 ^ (3|1)
    CHECK(EvalF("1 | 0 && 0") == doctest::Approx(0.f)); // (1|0) && 0, not 1 | (0&&0)
    CHECK(EvalF("0 && 1 || 1") == doctest::Approx(1.f)); // (0&&1) || 1, not 0 && (1||1)
  }

  TEST_CASE("expr: unary operators bind tighter than binary ones (#449)") {
    CHECK(EvalF("-2 + 3") == doctest::Approx(1.f));
    CHECK(EvalF("-(2 + 3)") == doctest::Approx(-5.f));
    CHECK(EvalF("- -5") == doctest::Approx(5.f));
    CHECK(EvalF("!0 + 1") == doctest::Approx(2.f)); // (!0) + 1
    CHECK(EvalF("~0") == doctest::Approx(-1.f));
    CHECK(EvalF("+7") == doctest::Approx(7.f)); // unary plus is the identity
    CHECK(EvalF("2 * -3") == doctest::Approx(-6.f));
  }

  TEST_CASE("expr: parentheses nest (#449)") {
    CHECK(EvalF("((((1 + 2))))") == doctest::Approx(3.f));
    CHECK(EvalF("(1 + (2 * (3 + (4 * 5))))") == doctest::Approx(47.f));
  }

  // The issue's own example.
  TEST_CASE("expr: the issue's example expression evaluates correctly (#449)") {
    CHECK(EvalF("($f1 * 0.5) + pow($f2, 2)", {8.f, 3.f}) == doctest::Approx(13.f));
  }

  // ─── C type rules ───────────────────────────────────────────────────────

  TEST_CASE("expr: arithmetic on two ints stays int (#449)") {
    CHECK(Eval("2 + 3").isInt);
    CHECK(Eval("2 - 3").isInt);
    CHECK(Eval("2 * 3").isInt);
    CHECK(Eval("$i1 * $i2", {2.f, 3.f}).isInt);
  }

  TEST_CASE("expr: one float operand makes the whole result a float (#449)") {
    CHECK_FALSE(Eval("2 + 3.").isInt);
    CHECK_FALSE(Eval("2. * 3").isInt);
    CHECK_FALSE(Eval("$f1 + 1", {2.f}).isInt);
    CHECK(Eval("2 + 3.5").AsFloat() == doctest::Approx(5.5f));
  }

  // The one result that surprises people, and the one Max gives too.
  TEST_CASE("expr: int over int is integer division (#449)") {
    CHECK(Eval("7 / 2").isInt);
    CHECK(Eval("7 / 2").AsInt() == 3);
    CHECK(Eval("$i1 / $i2", {7.f, 2.f}).AsInt() == 3);
    // Truncation is towards zero, as in C.
    CHECK(Eval("-7 / 2").AsInt() == -3);

    // Making either side a float restores the answer people expect.
    CHECK(Eval("7. / 2").AsFloat() == doctest::Approx(3.5f));
    CHECK(Eval("$f1 / $f2", {7.f, 2.f}).AsFloat() == doctest::Approx(3.5f));
  }

  TEST_CASE("expr: modulo is the integer remainder on ints and fmod otherwise (#449)") {
    CHECK(Eval("7 % 3").isInt);
    CHECK(Eval("7 % 3").AsInt() == 1);
    CHECK(Eval("-7 % 3").AsInt() == -1); // C's sign rule

    CHECK_FALSE(Eval("7.5 % 2.").isInt);
    CHECK(Eval("7.5 % 2.").AsFloat() == doctest::Approx(1.5f));
  }

  TEST_CASE("expr: comparisons, logic and bitwise operators always give an int (#449)") {
    for (const char* src : {"1.5 < 2.5", "1.5 == 1.5", "1.5 && 2.5", "!1.5", "~1", "3.9 & 1.1",
                            "1.5 | 2.5", "1.5 ^ 2.5", "1.5 << 1.5", "8.5 >> 1.5"}) {
      CAPTURE(src);
      CHECK(Eval(src).isInt);
    }
    CHECK(Eval("1.5 < 2.5").AsInt() == 1);
    CHECK(Eval("2.5 < 1.5").AsInt() == 0);
  }

  TEST_CASE("expr: comparison operators cover all six forms (#449)") {
    CHECK(EvalF("1 < 2") == 1.f);
    CHECK(EvalF("2 < 2") == 0.f);
    CHECK(EvalF("2 <= 2") == 1.f);
    CHECK(EvalF("3 > 2") == 1.f);
    CHECK(EvalF("2 >= 3") == 0.f);
    CHECK(EvalF("2 == 2") == 1.f);
    CHECK(EvalF("2 != 2") == 0.f);
  }

  TEST_CASE("expr: logical operators normalise to 0 or 1 (#449)") {
    CHECK(EvalF("5 && 7") == 1.f);
    CHECK(EvalF("5 && 0") == 0.f);
    CHECK(EvalF("0 || 0") == 0.f);
    CHECK(EvalF("0 || -3") == 1.f);
    CHECK(EvalF("!0") == 1.f);
    CHECK(EvalF("!5") == 0.f);
    CHECK(EvalF("!0.") == 1.f);
  }

  // Max reads ^ as exclusive-or, not exponentiation. Getting this wrong would
  // silently give a plausible-looking wrong answer, so it gets its own test.
  TEST_CASE("expr: ^ is bitwise exclusive-or, not a power (#449)") {
    CHECK(EvalF("2 ^ 3") == 1.f); // 0b10 ^ 0b11
    CHECK(EvalF("2 ^ 3") != 8.f); // ...and emphatically not 2**3
    CHECK(EvalF("pow(2, 3)") == doctest::Approx(8.f));
  }

  TEST_CASE("expr: bitwise operators truncate float operands (#449)") {
    CHECK(EvalF("12 & 10") == 8.f);
    CHECK(EvalF("12 | 3") == 15.f);
    CHECK(EvalF("12.9 & 10.9") == 8.f); // 12 & 10
    CHECK(EvalF("1 << 4") == 16.f);
    CHECK(EvalF("256 >> 4") == 16.f);
    CHECK(EvalF("-8 >> 1") == -4.f); // arithmetic shift
  }

  // ─── functions ──────────────────────────────────────────────────────────

  TEST_CASE("expr: every one-argument function is available (#449)") {
    CHECK(EvalF("sqrt(9.)") == doctest::Approx(3.f));
    CHECK(EvalF("exp(0.)") == doctest::Approx(1.f));
    CHECK(EvalF("ln(1.)") == doctest::Approx(0.f));
    CHECK(EvalF("log(1.)") == doctest::Approx(0.f)); // Max: log is the natural log
    CHECK(EvalF("ln(exp(2.))") == doctest::Approx(2.f));
    CHECK(EvalF("log10(1000.)") == doctest::Approx(3.f));
    CHECK(EvalF("sin(0.)") == doctest::Approx(0.f));
    CHECK(EvalF("cos(0.)") == doctest::Approx(1.f));
    CHECK(EvalF("tan(0.)") == doctest::Approx(0.f));
    CHECK(EvalF("asin(1.)") == doctest::Approx(1.5707963f));
    CHECK(EvalF("acos(1.)") == doctest::Approx(0.f));
    CHECK(EvalF("atan(1.)") == doctest::Approx(0.7853981f));
    CHECK(EvalF("sinh(0.)") == doctest::Approx(0.f));
    CHECK(EvalF("cosh(0.)") == doctest::Approx(1.f));
    CHECK(EvalF("tanh(0.)") == doctest::Approx(0.f));
    CHECK(EvalF("round(2.6)") == doctest::Approx(3.f));
    CHECK(EvalF("round(-2.6)") == doctest::Approx(-3.f));
    CHECK(EvalF("floor(2.9)") == doctest::Approx(2.f));
    CHECK(EvalF("ceil(2.1)") == doctest::Approx(3.f));
    CHECK(EvalF("fact(5)") == doctest::Approx(120.f));
    CHECK(EvalF("fact(0)") == doctest::Approx(1.f));
  }

  TEST_CASE("expr: every two-argument function is available (#449)") {
    CHECK(EvalF("min(3, 5)") == doctest::Approx(3.f));
    CHECK(EvalF("max(3, 5)") == doctest::Approx(5.f));
    CHECK(EvalF("pow(2., 10.)") == doctest::Approx(1024.f));
    CHECK(EvalF("atan2(1., 1.)") == doctest::Approx(0.7853981f));
  }

  TEST_CASE("expr: int() and float() convert (#449)") {
    CHECK(Eval("int(2.9)").isInt);
    CHECK(Eval("int(2.9)").AsInt() == 2);
    CHECK(Eval("int(-2.9)").AsInt() == -2); // towards zero

    CHECK_FALSE(Eval("float(2)").isInt);
    CHECK(Eval("float(2)").AsFloat() == doctest::Approx(2.f));

    // int() of a float divide is not the same as an int divide of the parts.
    CHECK(Eval("int(7. / 2.)").AsInt() == 3);
  }

  TEST_CASE("expr: abs, min and max keep the int type when every argument is one (#449)") {
    CHECK(Eval("abs(-5)").isInt);
    CHECK(Eval("abs(-5)").AsInt() == 5);
    CHECK(Eval("min(3, 5)").isInt);
    CHECK(Eval("max(3, 5)").isInt);

    CHECK_FALSE(Eval("abs(-5.5)").isInt);
    CHECK(Eval("abs(-5.5)").AsFloat() == doctest::Approx(5.5f));
    CHECK_FALSE(Eval("min(3., 5)").isInt);
    CHECK_FALSE(Eval("max(3, 5.)").isInt);
  }

  // Everything else returns a float, whatever it was handed. Stated once here
  // so the rule is pinned rather than implied by the values above.
  TEST_CASE("expr: the other functions always return a float (#449)") {
    for (const char* src : {"sqrt(4)", "pow(2, 3)", "round(2)", "floor(2)", "ceil(2)", "fact(3)",
                            "sin(0)", "atan2(1, 1)", "exp(0)", "ln(1)", "log10(1)"}) {
      CAPTURE(src);
      CHECK_FALSE(Eval(src).isInt);
    }
  }

  TEST_CASE("expr: functions nest and take expressions as arguments (#449)") {
    CHECK(EvalF("max(min(5, 3), 1)") == doctest::Approx(3.f));
    CHECK(EvalF("pow(1. + 1., 2. + 1.)") == doctest::Approx(8.f));
    CHECK(EvalF("sqrt(pow($f1, 2.) + pow($f2, 2.))", {3.f, 4.f}) == doctest::Approx(5.f));
  }

  // ─── the answers C leaves undefined ─────────────────────────────────────
  // Every one of these is a value a patch can actually produce, and every one
  // is undefined behaviour in C++. The answers match ./ , .% , .div , .<< and
  // .>>, so an expression and the equivalent chain of objects agree.

  TEST_CASE("expr: division by zero gives 0 rather than an infinity (#449)") {
    CHECK(EvalF("1 / 0") == 0.f);
    CHECK(EvalF("1. / 0.") == 0.f);
    CHECK(EvalF("$f1 / $f2", {5.f, 0.f}) == 0.f);
    CHECK(EvalF("1 % 0") == 0.f);
    CHECK(EvalF("1. % 0.") == 0.f);
  }

  TEST_CASE("expr: INT_MIN / -1 does not trap (#449)") {
    // The one quotient that does not fit back into an int — on x86 the divide
    // instruction faults and takes the process with it. .div answers it the
    // same way rather than letting that happen. (INT_MIN has to arrive through
    // an inlet: as source text, "-2147483648" is a negated literal too large
    // for an int, so C — and this parser — read it as a float.)
    const float intMin = (float)(-2147483647 - 1);
    CHECK(EvalF("$i1 / $i2", {intMin, -1.f}) == doctest::Approx(intMin));
    CHECK(EvalF("$i1 % $i2", {intMin, -1.f}) == 0.f);
  }

  TEST_CASE("expr: out-of-range shift counts are answered, not undefined (#449)") {
    CHECK(EvalF("1 << 64") == 0.f); // everything shifted out
    CHECK(EvalF("1 << -1") == 1.f); // a negative count shifts by nothing
    CHECK(EvalF("256 >> 64") == 0.f);
    CHECK(EvalF("-256 >> 64") == -1.f); // sign bit replicated
    CHECK(EvalF("256 >> -1") == 256.f);
  }

  TEST_CASE("expr: a non-finite result leaves as 0 (#449)") {
    CHECK(EvalF("sqrt(-1.)") == 0.f); // NaN
    CHECK(EvalF("ln(0.)") == 0.f); // -inf
    CHECK(EvalF("pow(10., 100.)") == 0.f); // overflows a float
    CHECK(EvalF("fact(40)") == 0.f); // 40! overflows a float
  }

  TEST_CASE("expr: a non-finite inlet value cannot poison the output (#449)") {
    CHECK(EvalF("$f1 + 1.", {kInf}) == 0.f);
    CHECK(EvalF("$f1 * 2.", {kNaN}) == 0.f);
    // ...but a comparison against one is still a well-defined int.
    CHECK(EvalF("$f1 > 0", {kInf}) == 1.f);
    CHECK(EvalF("$f1 == $f1", {kNaN}) == 0.f); // NaN != NaN, as in C
    // $i of a non-finite value is 0 rather than whatever the cast would do.
    CHECK(EvalF("$i1", {kNaN}) == 0.f);
    CHECK(EvalF("$i1", {kInf}) == 0.f);
    CHECK(EvalF("$i1", {1e30f}) == 0.f); // outside the int range
  }

  TEST_CASE("expr: fact of a negative number is 0, not a NaN (#449)") {
    CHECK(EvalF("fact(-3)") == 0.f);
  }

  TEST_CASE("expr: integer overflow wraps instead of trapping (#449)") {
    // Signed overflow is undefined in C++ and a sanitizer build would abort on
    // it; the evaluator wraps in two's complement instead.
    const ExprValue v = Eval("2147483647 + 1");
    CHECK(v.isInt);
    CHECK(v.AsInt() == (-2147483647 - 1));
    CHECK(Eval("$i1 - 1", {(float)(-2147483647 - 1)}).AsInt() == 2147483647);
    CHECK(Eval("2147483647 * 2").isInt);
  }

  // ─── malformed input ────────────────────────────────────────────────────
  // "Malformed expressions fail loudly at parse time, not silently at render
  // time" is an acceptance criterion, so every rejection is named.

  TEST_CASE("expr: an empty expression is rejected (#449)") {
    CHECK(CompileError("") == "expression is empty");
    CHECK(CompileError("   ") == "expression is empty");
  }

  TEST_CASE("expr: an unbalanced parenthesis is rejected (#449)") {
    CHECK(CompileError("(1 + 2").find("missing ')'") != std::string::npos);
    CHECK_FALSE(CompileError("1 + 2)").empty());
    CHECK_FALSE(CompileError(")").empty());
    CHECK_FALSE(CompileError("((1)").empty());
  }

  TEST_CASE("expr: a missing operand is rejected (#449)") {
    CHECK_FALSE(CompileError("1 +").empty());
    CHECK_FALSE(CompileError("* 2").empty());
    CHECK_FALSE(CompileError("1 * / 2").empty());
    CHECK_FALSE(CompileError("()").empty());
    CHECK_FALSE(CompileError("-").empty());
  }

  TEST_CASE("expr: two adjacent operands are rejected (#449)") {
    CHECK(CompileError("1 2").find("trailing") != std::string::npos);
    CHECK_FALSE(CompileError("$f1 $f2").empty());
  }

  TEST_CASE("expr: an unknown character is rejected by name (#449)") {
    const std::string e = CompileError("1 @ 2");
    CHECK(e.find("unexpected character '@'") != std::string::npos);
    CHECK(e.find("character 3") != std::string::npos); // 1-based position
  }

  TEST_CASE("expr: a single '=' is rejected with a hint (#449)") {
    CHECK(CompileError("$f1 = 2").find("did you mean '=='") != std::string::npos);
  }

  TEST_CASE("expr: a malformed $ placeholder is rejected (#449)") {
    CHECK(CompileError("$").find("must be followed by") != std::string::npos);
    CHECK(CompileError("$x1").find("is not a variable") != std::string::npos);
    CHECK(CompileError("$f").find("inlet number") != std::string::npos);
    CHECK(CompileError("$f0").find("1-9") != std::string::npos);
    CHECK(CompileError("$f10").find("1-9") != std::string::npos);
    CHECK(CompileError("$i12 + 1").find("1-9") != std::string::npos);
  }

  // Max's table access needs a `table` object the YSE patcher does not have.
  // Saying so beats "unexpected character".
  TEST_CASE("expr: $s table access is rejected with an explanation (#449)") {
    const std::string e = CompileError("$s2[7]");
    CHECK(e.find("table access") != std::string::npos);
    CHECK(e.find("not supported") != std::string::npos);
  }

  TEST_CASE("expr: an unknown function is rejected by name (#449)") {
    CHECK(CompileError("frobnicate(1)").find("unknown function 'frobnicate'") != std::string::npos);
  }

  // The Max functions this evaluator deliberately leaves out get a different
  // message from a typo, so a user porting a patch knows which it is.
  TEST_CASE("expr: an unsupported Max function is rejected with an explanation (#449)") {
    for (const char* src :
         {"random(0, 1)", "noise()", "size($s1)", "sum($s1)", "avg($s1)", "store($s1, 1)"}) {
      CAPTURE(src);
      const std::string e = CompileError(src);
      CHECK(e.find("not supported") != std::string::npos);
    }
    CHECK(CompileError("random(0, 1)").find("'random'") != std::string::npos);
  }

  TEST_CASE("expr: the wrong number of function arguments is rejected (#449)") {
    CHECK(CompileError("min(1)").find("takes 2 arguments, got 1") != std::string::npos);
    CHECK(CompileError("min(1, 2, 3)").find("takes 2 arguments, got 3") != std::string::npos);
    CHECK(CompileError("sqrt(1, 2)").find("takes 1 argument, got 2") != std::string::npos);
    CHECK(CompileError("sqrt()").find("takes 1 argument, got 0") != std::string::npos);
  }

  TEST_CASE("expr: a function name without a call is rejected (#449)") {
    CHECK(CompileError("sqrt").find("must be called with '('") != std::string::npos);
    CHECK_FALSE(CompileError("1 + sqrt").empty());
    CHECK_FALSE(CompileError("min(1, 2").empty());
  }

  // A hostile creation argument must not be able to run the parser off its own
  // C++ stack, nor build a program that would overrun the evaluator's fixed
  // value stack. Both are capped at compile time rather than left to the OS,
  // which is also why Evaluate() needs no bounds check per instruction.
  TEST_CASE("expr: pathological nesting is rejected instead of overflowing (#449)") {
    std::string parens;
    for (int i = 0; i < 500; i++)
      parens += '(';
    parens += '1';
    for (int i = 0; i < 500; i++)
      parens += ')';
    CHECK_FALSE(CompileError(parens).empty());

    std::string unaries(500, '-');
    unaries += '1';
    CHECK_FALSE(CompileError(unaries).empty());

    // Right-nesting is what grows the evaluation stack: every level pushes an
    // operand before recursing.
    std::string deep;
    for (int i = 0; i < 200; i++)
      deep += "(1 + ";
    deep += '1';
    for (int i = 0; i < 200; i++)
      deep += ')';
    CHECK_FALSE(CompileError(deep).empty());

    std::string calls;
    for (int i = 0; i < 200; i++)
      calls += "min(1, ";
    calls += '1';
    for (int i = 0; i < 200; i++)
      calls += ')';
    CHECK_FALSE(CompileError(calls).empty());

    // ...while a reasonable amount of nesting still compiles, and what does
    // compile is guaranteed to fit the evaluator's stack.
    CHECK(EvalF("((((((((1 + 1))))))))") == doctest::Approx(2.f));
    CHECK(Compiled("((((((((1 + 1))))))))").StackDepth() <= YSE::PATCHER::kExprMaxStack);
  }

  TEST_CASE("expr: a rejected expression leaves the program inert (#449)") {
    ExprProgram p;
    REQUIRE(p.Compile("1 + 2"));
    CHECK(p.Valid());
    CHECK(p.Size() > 0);

    CHECK_FALSE(p.Compile("1 +"));
    CHECK_FALSE(p.Valid());
    CHECK(p.Size() == 0);
    CHECK_FALSE(p.Error().empty());

    float vars[kExprMaxVars] = {};
    CHECK(p.Evaluate(vars).AsInt() == 0);

    // ...and a later good compile clears the error.
    REQUIRE(p.Compile("3 * 4"));
    CHECK(p.Error().empty());
    CHECK(p.Evaluate(vars).AsInt() == 12);
  }

  // ─── the RT contract ────────────────────────────────────────────────────

  TEST_CASE("expr: evaluating does not change the compiled program (#449)") {
    // The whole RT argument in one assertion: Evaluate() is const and touches
    // no state that could allocate. If it ever grew the instruction vector,
    // resolved a name, or cached anything, this would move.
    ExprProgram p = Compiled("sqrt(pow($f1, 2.) + pow($f2, 2.)) * $i3");
    const std::size_t size = p.Size();
    const int depth = p.StackDepth();

    for (int i = 0; i < 1000; i++) {
      float vars[kExprMaxVars] = {};
      vars[0] = (float)i;
      vars[1] = (float)(i * 2);
      vars[2] = 3.f;
      const ExprValue v = p.Evaluate(vars);
      CHECK(std::isfinite(v.AsFloat()));
    }
    CHECK(p.Size() == size);
    CHECK(p.StackDepth() == depth);
    CHECK(p.StackDepth() <= YSE::PATCHER::kExprMaxStack);
  }

  TEST_CASE("expr: the same program re-evaluates from different inlet values (#449)") {
    const ExprProgram p = Compiled("$f1 * $f2");
    float vars[kExprMaxVars] = {};

    vars[0] = 3.f;
    vars[1] = 4.f;
    CHECK(p.Evaluate(vars).AsFloat() == doctest::Approx(12.f));

    vars[1] = 0.5f;
    CHECK(p.Evaluate(vars).AsFloat() == doctest::Approx(1.5f));
  }

  // ─── the shared list parser ─────────────────────────────────────────────
  // .vexpr (#450) reads its whole input as a list, so the allocation-free
  // splitter lives next to the evaluator rather than inside .expr.

  TEST_CASE("expr: the shared list parser reads numbers without allocating (#449)") {
    float out[kExprMaxVars] = {};

    CHECK(YSE::PATCHER::ExprParseFloatList("1 2 3", out, kExprMaxVars) == 3);
    CHECK(out[0] == doctest::Approx(1.f));
    CHECK(out[2] == doctest::Approx(3.f));

    CHECK(YSE::PATCHER::ExprParseFloatList("  -1.5   2e2 ", out, kExprMaxVars) == 2);
    CHECK(out[0] == doctest::Approx(-1.5f));
    CHECK(out[1] == doctest::Approx(200.f));

    // Non-numeric items are skipped, not treated as a terminator.
    CHECK(YSE::PATCHER::ExprParseFloatList("1 bang 3", out, kExprMaxVars) == 2);
    CHECK(out[1] == doctest::Approx(3.f));

    // The cap is honoured; the empty and null cases are answered.
    CHECK(YSE::PATCHER::ExprParseFloatList("1 2 3 4 5", out, 2) == 2);
    CHECK(YSE::PATCHER::ExprParseFloatList("", out, kExprMaxVars) == 0);
    CHECK(YSE::PATCHER::ExprParseFloatList(nullptr, out, kExprMaxVars) == 0);
  }

  // ═══ the object ═══════════════════════════════════════════════════════════

  TEST_CASE("expr: the object is creatable through the registry (#449)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_EXPR);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".expr"));
    // With no expression there is still a left inlet to bang.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 1);
    CHECK(h->OutputDataType(0) == YSE::OUT_TYPE::ANY);
  }

  TEST_CASE("expr: the object is listed by pRegistry::AllNames (#449)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".expr")) != names.end());
  }

  TEST_CASE("expr: the expression grows the inlets (#449)") {
    YSE::patcher p;
    p.create(2);

    YSE::pHandle* one = p.CreateObject(YSE::OBJ::G_EXPR, "$f1 + 1");
    REQUIRE(one != nullptr);
    CHECK(one->GetInputs() == 1);

    YSE::pHandle* three = p.CreateObject(YSE::OBJ::G_EXPR, "$f1 + $f2 + $f3");
    REQUIRE(three != nullptr);
    CHECK(three->GetInputs() == 3);

    // A gap still gets its inlet, so the numbering matches the placeholders.
    YSE::pHandle* gap = p.CreateObject(YSE::OBJ::G_EXPR, "$f1 * $f4");
    REQUIRE(gap != nullptr);
    CHECK(gap->GetInputs() == 4);

    YSE::pHandle* all = p.CreateObject(YSE::OBJ::G_EXPR, "$i9");
    REQUIRE(all != nullptr);
    CHECK(all->GetInputs() == 9);
  }

  TEST_CASE("expr: a malformed expression leaves one inlet and evaluates to 0 (#449)") {
    ExprRig rig("$f1 +");
    CHECK_FALSE(rig.op.CompileError().empty());
    CHECK(rig.op.NumInputs() == 1);

    rig.Feed(5.f);
    CHECK(rig.Out() == 0.f);
  }

  TEST_CASE("expr: inlet 0 evaluates and the others only store (#449)") {
    ExprRig rig("$f1 + $f2 + $f3");
    REQUIRE(rig.op.NumInputs() == 3);

    rig.Reset();
    rig.Set(1, 10.f);
    CHECK_FALSE(rig.sink.gotFloat);
    CHECK_FALSE(rig.sink.gotInt);
    rig.Set(2, 100.f);
    CHECK_FALSE(rig.sink.gotFloat);
    CHECK_FALSE(rig.sink.gotInt);

    rig.Feed(1.f);
    CHECK(rig.sink.gotFloat);
    CHECK(rig.Out() == doctest::Approx(111.f));
  }

  TEST_CASE("expr: stored values survive between evaluations (#449)") {
    ExprRig rig("$f1 * $f2");
    rig.Set(1, 3.f);

    rig.Feed(2.f);
    CHECK(rig.Out() == doctest::Approx(6.f));

    rig.Feed(5.f);
    CHECK(rig.Out() == doctest::Approx(15.f)); // inlet 1 still 3

    rig.Set(1, 10.f);
    rig.Feed(5.f);
    CHECK(rig.Out() == doctest::Approx(50.f));
  }

  TEST_CASE("expr: a bang on inlet 0 re-evaluates with the stored values (#449)") {
    ExprRig rig("$f1 + $f2");
    rig.Set(1, 7.f);
    rig.Feed(1.f);
    CHECK(rig.Out() == doctest::Approx(8.f));

    rig.Bang();
    CHECK(rig.sink.gotFloat);
    CHECK(rig.Out() == doctest::Approx(8.f));

    // A bang before anything arrives evaluates the zero-initialised inlets.
    ExprRig fresh("$f1 + $f2");
    fresh.Bang();
    CHECK(fresh.sink.gotFloat);
    CHECK(fresh.Out() == doctest::Approx(0.f));
  }

  TEST_CASE("expr: an int on any inlet is accepted (#449)") {
    ExprRig rig("$f1 - $f2");
    rig.op.GetInlet(1)->SetInt(4, YSE::T_GUI);
    rig.Reset();
    rig.op.GetInlet(0)->SetInt(10, YSE::T_GUI);
    CHECK(rig.Out() == doctest::Approx(6.f));
  }

  TEST_CASE("expr: a list on inlet 0 fills the inlets and evaluates (#449)") {
    ExprRig rig("$f1 + $f2 * $f3");
    REQUIRE(rig.op.NumInputs() == 3);

    rig.FeedList("1 2 3");
    CHECK(rig.Out() == doctest::Approx(7.f)); // 1 + 2*3

    // "If the list contains fewer items than there are inlets, the most
    // recently received value in each remaining inlet is used."
    rig.FeedList("10");
    CHECK(rig.Out() == doctest::Approx(16.f)); // 10 + 2*3

    // Extra items past the ninth inlet are simply dropped.
    rig.FeedList("1 1 1 1 1 1 1 1 1 1 1 1");
    CHECK(rig.Out() == doctest::Approx(2.f));
  }

  TEST_CASE("expr: the outlet sends an int or a float following the expression (#449)") {
    ExprRig ints("$i1 * 2");
    ints.Feed(3.4f);
    CHECK(ints.sink.gotInt);
    CHECK_FALSE(ints.sink.gotFloat);
    CHECK(ints.sink.intValue == 6);

    ExprRig floats("$f1 * 2.");
    floats.Feed(3.5f);
    CHECK(floats.sink.gotFloat);
    CHECK_FALSE(floats.sink.gotInt);
    CHECK(floats.sink.floatValue == doctest::Approx(7.f));
  }

  // The use case from the issue: one object where a dozen wired boxes would go.
  TEST_CASE("expr: replaces a chain of arithmetic objects (#449)") {
    ExprRig rig("($f1 * 0.5) + pow($f2, 2)");
    REQUIRE(rig.op.NumInputs() == 2);
    rig.Set(1, 3.f);
    rig.Feed(8.f);
    CHECK(rig.Out() == doctest::Approx(13.f));

    rig.Set(1, 4.f);
    rig.Feed(10.f);
    CHECK(rig.Out() == doctest::Approx(21.f));
  }

  // ─── params / persistence ───────────────────────────────────────────────

  TEST_CASE("expr: re-setting the expression recompiles and resizes (#449)") {
    YSE::PATCHER::gExpr op;
    CHECK(op.NumInputs() == 1);

    op.SetParams("$f1 + $f2 + $f3");
    CHECK(op.NumInputs() == 3);
    CHECK(op.CompileError().empty());

    // Shrinking works as well as growing.
    op.SetParams("$f1 * 2.");
    CHECK(op.NumInputs() == 1);
    CHECK(op.Program().InletCount() == 1);

    // An empty argument returns the object to its blank state.
    op.SetParams("");
    CHECK(op.NumInputs() == 1);
    CHECK_FALSE(op.Program().Valid());
  }

  TEST_CASE("expr: re-setting the expression clears the stored inlet values (#449)") {
    ExprRig rig("$f1 + $f2");
    rig.Set(1, 100.f);
    rig.Feed(1.f);
    CHECK(rig.Out() == doctest::Approx(101.f));

    // The new expression re-uses inlet 1 for something else; the old value must
    // not leak into it. (In a live patcher SetParams builds a fresh object, so
    // this only matters for standalone use — which is exactly what a binding
    // driving pObject directly does.)
    rig.op.SetParams("$f1 - $f2");
    rig.Feed(1.f);
    CHECK(rig.Out() == doctest::Approx(1.f));
  }

  TEST_CASE("expr: params survive a DumpJSON / ParseJSON round trip (#449)") {
    const std::string source = "($f1 * 0.5) + pow($f2, 2)";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_EXPR, source) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".expr") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".expr"));
    // The spacing the caller typed is preserved verbatim, not the normalised
    // spelling the compiler works on.
    CHECK(h->GetParams() == source);
    // ...and the reloaded object really did recompile it.
    CHECK(h->GetInputs() == 2);
  }

  TEST_CASE("expr: an operator-dense expression survives the params round trip (#449)") {
    // The param string is split on spaces and glued back together, so an
    // expression whose spacing is unusual is the interesting case.
    const std::string source = "$i1<<2|$i2&3";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_EXPR, source) != nullptr);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(src.DumpJSON());
    REQUIRE(loaded.Objects() == 1);
    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(h->GetParams() == source);
    CHECK(h->GetInputs() == 2);
  }

  TEST_CASE("expr: names its single parameter (#449)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_EXPR));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 1);
    CHECK(docs[0].name == "expression");
  }

  // ─── documentation ──────────────────────────────────────────────────────

  TEST_CASE("expr: documents itself as MATH with a labelled port pair (#449)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_EXPR));
    REQUIRE(obj != nullptr);
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::MATH);
    CHECK_FALSE(obj->GetDescription().empty());

    REQUIRE(obj->NumInputs() == 1);
    CHECK(obj->GetInlet(0)->GetDocLabel() == std::string("$1"));
    CHECK_FALSE(obj->GetInlet(0)->GetDocDescription().empty());

    REQUIRE(obj->NumOutputs() == 1);
    CHECK(obj->GetOutputType(0) == YSE::OUT_TYPE::ANY);
    CHECK(obj->GetOutlet(0)->GetDocLabel() == std::string("out"));
  }

  // The doc-coverage test only sees the object the registry builds, which has
  // one inlet; the inlets ParseParams adds must carry labels too.
  TEST_CASE("expr: the inlets created from the expression are documented (#449)") {
    YSE::PATCHER::gExpr op;
    op.SetParams("$f1 + $f3");
    REQUIRE(op.NumInputs() == 3);

    const std::vector<std::string> labels = {"$1", "$2", "$3"};
    for (int i = 0; i < 3; i++) {
      CAPTURE(i);
      CHECK(op.GetInlet(i)->GetDocLabel() == labels[(size_t)i]);
      CHECK_FALSE(op.GetInlet(i)->GetDocDescription().empty());
    }
    // The gap inlet says it is a gap rather than pretending to be useful.
    CHECK(op.GetInlet(1)->GetDocDescription().find("Unused") != std::string::npos);
  }

} // TEST_SUITE("patcher")

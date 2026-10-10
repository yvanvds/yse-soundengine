#include "gExprEval.h"

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>

using namespace YSE::PATCHER;

namespace {

  // ─── tokens ───────────────────────────────────────────────────────────────

  enum class Tok : unsigned char {
    END,
    INT_LIT,
    FLOAT_LIT,
    VAR_INT, // $iN
    VAR_FLOAT, // $fN
    IDENT, // a function name
    LPAREN,
    RPAREN,
    COMMA,
    PLUS,
    MINUS,
    STAR,
    SLASH,
    PERCENT,
    SHL,
    SHR,
    LT,
    LE,
    GT,
    GE,
    EQ,
    NE,
    AMP,
    CARET,
    PIPE,
    ANDAND,
    OROR,
    BANG,
    TILDE,
  };

  struct Token {
    Tok kind = Tok::END;
    int i = 0; // INT_LIT value, or the 0-based variable slot
    float f = 0.f; // FLOAT_LIT value
    std::string text; // IDENT spelling
    std::size_t pos = 0; // character offset, for error messages
  };

  struct FuncDef {
    const char* name;
    ExprOp op;
    int arity;
  };

  // The function set, in Max's own listing order where it overlaps. ``abs`` is
  // the one addition: C has it, the patcher has ``.abs``, and every alternative
  // spelling in an expression is clumsier.
  const FuncDef kFunctions[] = {
      {"min", ExprOp::F_MIN, 2},     {"max", ExprOp::F_MAX, 2},     {"int", ExprOp::F_INT, 1},
      {"float", ExprOp::F_FLOAT, 1}, {"abs", ExprOp::F_ABS, 1},     {"pow", ExprOp::F_POW, 2},
      {"sqrt", ExprOp::F_SQRT, 1},   {"exp", ExprOp::F_EXP, 1},     {"log10", ExprOp::F_LOG10, 1},
      {"ln", ExprOp::F_LN, 1},       {"log", ExprOp::F_LN, 1},      {"sin", ExprOp::F_SIN, 1},
      {"cos", ExprOp::F_COS, 1},     {"tan", ExprOp::F_TAN, 1},     {"asin", ExprOp::F_ASIN, 1},
      {"acos", ExprOp::F_ACOS, 1},   {"atan", ExprOp::F_ATAN, 1},   {"atan2", ExprOp::F_ATAN2, 2},
      {"sinh", ExprOp::F_SINH, 1},   {"cosh", ExprOp::F_COSH, 1},   {"tanh", ExprOp::F_TANH, 1},
      {"fact", ExprOp::F_FACT, 1},   {"round", ExprOp::F_ROUND, 1}, {"floor", ExprOp::F_FLOOR, 1},
      {"ceil", ExprOp::F_CEIL, 1},
  };

  // Functions Max's expr has that this evaluator deliberately leaves out, so
  // the error can say *why* rather than "unknown function". The table ones need
  // a `table` object the YSE patcher does not have; the stochastic ones would
  // make the evaluator stateful, and .random already exists.
  const char* const kUnsupportedFunctions[] = {"random", "noise", "size", "sum",
                                               "Sum",    "avg",   "Avg",  "store"};

  const FuncDef* FindFunction(const std::string& name) {
    for (const FuncDef& f : kFunctions) {
      if (name == f.name) return &f;
    }
    return nullptr;
  }

  bool IsUnsupportedFunction(const std::string& name) {
    for (const char* f : kUnsupportedFunctions) {
      if (name == f) return true;
    }
    return false;
  }

  bool IsDigit(char c) {
    return c >= '0' && c <= '9';
  }

  bool IsIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  }

  bool IsIdentChar(char c) {
    return IsIdentStart(c) || IsDigit(c);
  }

  std::string At(std::size_t pos) {
    return " at character " + std::to_string(pos + 1);
  }

  // ─── compiler ─────────────────────────────────────────────────────────────
  //
  // Control thread only: a tokenizer, a recursive-descent parser over C's
  // precedence ladder, and a lowering pass that emits straight into the
  // postfix instruction vector as it reduces. Nothing here runs at render time.

  struct Compiler {
    std::vector<ExprInstr>* code = nullptr;
    std::string error;
    unsigned int usedInlets = 0;
    int maxVar = -1; // highest 0-based slot referenced
    int depth = 0; // current stack depth while emitting
    int maxDepth = 0;

    std::vector<Token> tokens;
    std::size_t p = 0;

    bool Fail(const std::string& msg) {
      if (error.empty()) error = msg;
      return false;
    }

    // ── tokenizer ──

    bool Tokenize(const std::string& s) {
      std::size_t i = 0;
      while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
          i++;
          continue;
        }

        Token t;
        t.pos = i;

        if (IsDigit(c) || (c == '.' && i + 1 < s.size() && IsDigit(s[i + 1]))) {
          if (!ScanNumber(s, i, t)) return false;
          tokens.push_back(t);
          continue;
        }

        if (c == '$') {
          if (!ScanVariable(s, i, t)) return false;
          tokens.push_back(t);
          continue;
        }

        if (IsIdentStart(c)) {
          const std::size_t start = i;
          while (i < s.size() && IsIdentChar(s[i]))
            i++;
          t.kind = Tok::IDENT;
          t.text = s.substr(start, i - start);
          tokens.push_back(t);
          continue;
        }

        // Two-character operators first, so `<<` never reads as `<` `<`.
        const char n = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (c == '<' && n == '<') {
          t.kind = Tok::SHL;
          i += 2;
        } else if (c == '>' && n == '>') {
          t.kind = Tok::SHR;
          i += 2;
        } else if (c == '<' && n == '=') {
          t.kind = Tok::LE;
          i += 2;
        } else if (c == '>' && n == '=') {
          t.kind = Tok::GE;
          i += 2;
        } else if (c == '=' && n == '=') {
          t.kind = Tok::EQ;
          i += 2;
        } else if (c == '!' && n == '=') {
          t.kind = Tok::NE;
          i += 2;
        } else if (c == '&' && n == '&') {
          t.kind = Tok::ANDAND;
          i += 2;
        } else if (c == '|' && n == '|') {
          t.kind = Tok::OROR;
          i += 2;
        } else {
          switch (c) {
          case '(':
            t.kind = Tok::LPAREN;
            break;
          case ')':
            t.kind = Tok::RPAREN;
            break;
          case ',':
            t.kind = Tok::COMMA;
            break;
          case '+':
            t.kind = Tok::PLUS;
            break;
          case '-':
            t.kind = Tok::MINUS;
            break;
          case '*':
            t.kind = Tok::STAR;
            break;
          case '/':
            t.kind = Tok::SLASH;
            break;
          case '%':
            t.kind = Tok::PERCENT;
            break;
          case '<':
            t.kind = Tok::LT;
            break;
          case '>':
            t.kind = Tok::GT;
            break;
          case '&':
            t.kind = Tok::AMP;
            break;
          case '^':
            t.kind = Tok::CARET;
            break;
          case '|':
            t.kind = Tok::PIPE;
            break;
          case '!':
            t.kind = Tok::BANG;
            break;
          case '~':
            t.kind = Tok::TILDE;
            break;
          case '=':
            return Fail("'=' is not an operator; did you mean '=='?" + At(i));
          default:
            return Fail(std::string("unexpected character '") + c + "'" + At(i));
          }
          i++;
        }
        tokens.push_back(t);
      }

      Token end;
      end.kind = Tok::END;
      end.pos = s.size();
      tokens.push_back(end);
      return true;
    }

    bool ScanNumber(const std::string& s, std::size_t& i, Token& t) {
      const std::size_t start = i;
      bool isFloat = false;

      while (i < s.size() && IsDigit(s[i]))
        i++;
      if (i < s.size() && s[i] == '.') {
        isFloat = true;
        i++;
        while (i < s.size() && IsDigit(s[i]))
          i++;
      }
      if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        // Only an exponent if real digits follow; otherwise the 'e' belongs to
        // whatever identifier comes next and the number ends here.
        std::size_t q = i + 1;
        if (q < s.size() && (s[q] == '+' || s[q] == '-')) q++;
        if (q < s.size() && IsDigit(s[q])) {
          isFloat = true;
          i = q;
          while (i < s.size() && IsDigit(s[i]))
            i++;
        }
      }

      const char* first = s.c_str() + start;
      if (!isFloat) {
        errno = 0;
        const long v = std::strtol(first, nullptr, 10);
        if (errno == 0 && v >= INT_MIN && v <= INT_MAX) {
          t.kind = Tok::INT_LIT;
          t.i = (int)v;
          return true;
        }
        // Too large for an int: keep the value rather than the type. C would
        // do the same by promoting the literal.
      }
      t.kind = Tok::FLOAT_LIT;
      t.f = std::strtof(first, nullptr);
      return true;
    }

    bool ScanVariable(const std::string& s, std::size_t& i, Token& t) {
      const std::size_t dollar = i;
      i++; // '$'
      if (i >= s.size()) return Fail("'$' must be followed by i, f or s" + At(dollar));

      const char type = s[i];
      if (type == 's') {
        return Fail("$s (table access) is not supported by this expression evaluator" + At(dollar));
      }
      if (type != 'i' && type != 'f') {
        return Fail(std::string("'$") + type + "' is not a variable; use $i, $f (or $s in Max)" +
                    At(dollar));
      }
      i++;
      if (i >= s.size() || !IsDigit(s[i])) {
        return Fail("$ variable needs an inlet number, 1-9" + At(dollar));
      }
      const int index = s[i] - '0';
      i++;
      // A second digit would mean an inlet past the ninth, which Max does not
      // have either. Catch it here rather than silently reading "$f12" as
      // "$f1" followed by a stray 2.
      if (index < 1 || index > kExprMaxVars || (i < s.size() && IsDigit(s[i]))) {
        return Fail("inlet number must be 1-" + std::to_string(kExprMaxVars) + At(dollar));
      }

      t.kind = (type == 'i') ? Tok::VAR_INT : Tok::VAR_FLOAT;
      t.i = index - 1;
      return true;
    }

    // ── emit ──

    bool Emit(ExprOp op, int i, float f, int effect) {
      ExprInstr instr;
      instr.op = op;
      instr.i = i;
      instr.f = f;
      code->push_back(instr);
      depth += effect;
      if (depth > maxDepth) maxDepth = depth;
      if (maxDepth > kExprMaxStack) {
        return Fail("expression is too deeply nested to evaluate (needs more than " +
                    std::to_string(kExprMaxStack) + " stack slots)");
      }
      return true;
    }

    bool EmitUnary(ExprOp op) {
      return Emit(op, 0, 0.f, 0);
    }
    bool EmitBinary(ExprOp op) {
      return Emit(op, 0, 0.f, -1);
    }

    // ── parser ──

    const Token& Peek() const {
      return tokens[p];
    }

    bool ParseExpression(int d) {
      if (d > kExprMaxDepth) return Fail("expression nests too deeply to parse");
      return ParseLogicalOr(d);
    }

    bool ParseLogicalOr(int d) {
      if (!ParseLogicalAnd(d)) return false;
      while (Peek().kind == Tok::OROR) {
        p++;
        if (!ParseLogicalAnd(d)) return false;
        if (!EmitBinary(ExprOp::OR)) return false;
      }
      return true;
    }

    bool ParseLogicalAnd(int d) {
      if (!ParseBitOr(d)) return false;
      while (Peek().kind == Tok::ANDAND) {
        p++;
        if (!ParseBitOr(d)) return false;
        if (!EmitBinary(ExprOp::AND)) return false;
      }
      return true;
    }

    bool ParseBitOr(int d) {
      if (!ParseBitXor(d)) return false;
      while (Peek().kind == Tok::PIPE) {
        p++;
        if (!ParseBitXor(d)) return false;
        if (!EmitBinary(ExprOp::BITOR)) return false;
      }
      return true;
    }

    bool ParseBitXor(int d) {
      if (!ParseBitAnd(d)) return false;
      while (Peek().kind == Tok::CARET) {
        p++;
        if (!ParseBitAnd(d)) return false;
        if (!EmitBinary(ExprOp::BITXOR)) return false;
      }
      return true;
    }

    bool ParseBitAnd(int d) {
      if (!ParseEquality(d)) return false;
      while (Peek().kind == Tok::AMP) {
        p++;
        if (!ParseEquality(d)) return false;
        if (!EmitBinary(ExprOp::BITAND)) return false;
      }
      return true;
    }

    bool ParseEquality(int d) {
      if (!ParseRelational(d)) return false;
      for (;;) {
        const Tok k = Peek().kind;
        if (k != Tok::EQ && k != Tok::NE) return true;
        p++;
        if (!ParseRelational(d)) return false;
        if (!EmitBinary(k == Tok::EQ ? ExprOp::EQ : ExprOp::NE)) return false;
      }
    }

    bool ParseRelational(int d) {
      if (!ParseShift(d)) return false;
      for (;;) {
        const Tok k = Peek().kind;
        ExprOp op;
        switch (k) {
        case Tok::LT:
          op = ExprOp::LT;
          break;
        case Tok::LE:
          op = ExprOp::LE;
          break;
        case Tok::GT:
          op = ExprOp::GT;
          break;
        case Tok::GE:
          op = ExprOp::GE;
          break;
        default:
          return true;
        }
        p++;
        if (!ParseShift(d)) return false;
        if (!EmitBinary(op)) return false;
      }
    }

    bool ParseShift(int d) {
      if (!ParseAdditive(d)) return false;
      for (;;) {
        const Tok k = Peek().kind;
        if (k != Tok::SHL && k != Tok::SHR) return true;
        p++;
        if (!ParseAdditive(d)) return false;
        if (!EmitBinary(k == Tok::SHL ? ExprOp::SHL : ExprOp::SHR)) return false;
      }
    }

    bool ParseAdditive(int d) {
      if (!ParseMultiplicative(d)) return false;
      for (;;) {
        const Tok k = Peek().kind;
        if (k != Tok::PLUS && k != Tok::MINUS) return true;
        p++;
        if (!ParseMultiplicative(d)) return false;
        if (!EmitBinary(k == Tok::PLUS ? ExprOp::ADD : ExprOp::SUB)) return false;
      }
    }

    bool ParseMultiplicative(int d) {
      if (!ParseUnary(d)) return false;
      for (;;) {
        const Tok k = Peek().kind;
        ExprOp op;
        switch (k) {
        case Tok::STAR:
          op = ExprOp::MUL;
          break;
        case Tok::SLASH:
          op = ExprOp::DIV;
          break;
        case Tok::PERCENT:
          op = ExprOp::MOD;
          break;
        default:
          return true;
        }
        p++;
        if (!ParseUnary(d)) return false;
        if (!EmitBinary(op)) return false;
      }
    }

    bool ParseUnary(int d) {
      if (d > kExprMaxDepth) return Fail("expression nests too deeply to parse");
      const Tok k = Peek().kind;
      if (k == Tok::MINUS) {
        p++;
        if (!ParseUnary(d + 1)) return false;
        return EmitUnary(ExprOp::NEG);
      }
      if (k == Tok::PLUS) {
        p++;
        return ParseUnary(d + 1); // unary plus is the identity, emit nothing
      }
      if (k == Tok::BANG) {
        p++;
        if (!ParseUnary(d + 1)) return false;
        return EmitUnary(ExprOp::NOT);
      }
      if (k == Tok::TILDE) {
        p++;
        if (!ParseUnary(d + 1)) return false;
        return EmitUnary(ExprOp::BITNOT);
      }
      return ParsePrimary(d);
    }

    bool ParsePrimary(int d) {
      if (d > kExprMaxDepth) return Fail("expression nests too deeply to parse");
      const Token& t = Peek();
      switch (t.kind) {
      case Tok::INT_LIT:
        p++;
        return Emit(ExprOp::PUSH_INT, t.i, 0.f, 1);

      case Tok::FLOAT_LIT: {
        const float value = t.f;
        p++;
        return Emit(ExprOp::PUSH_FLOAT, 0, value, 1);
      }

      case Tok::VAR_INT:
      case Tok::VAR_FLOAT: {
        const int slot = t.i;
        const bool asInt = (t.kind == Tok::VAR_INT);
        p++;
        usedInlets |= (1u << slot);
        if (slot > maxVar) maxVar = slot;
        return Emit(asInt ? ExprOp::PUSH_VAR_INT : ExprOp::PUSH_VAR_FLOAT, slot, 0.f, 1);
      }

      case Tok::LPAREN: {
        p++;
        if (!ParseExpression(d + 1)) return false;
        if (Peek().kind != Tok::RPAREN) {
          return Fail("missing ')'" + At(Peek().pos));
        }
        p++;
        return true;
      }

      case Tok::IDENT:
        return ParseCall(d);

      case Tok::END:
        return Fail("unexpected end of expression — an operand is missing");

      default:
        return Fail("expected a number, a $ variable or '(' " + At(t.pos));
      }
    }

    bool ParseCall(int d) {
      const Token name = tokens[p];
      p++;

      const FuncDef* fn = FindFunction(name.text);
      if (fn == nullptr) {
        if (IsUnsupportedFunction(name.text)) {
          return Fail("function '" + name.text + "' is not supported by this expression evaluator" +
                      At(name.pos));
        }
        return Fail("unknown function '" + name.text + "'" + At(name.pos));
      }

      if (Peek().kind != Tok::LPAREN) {
        return Fail("'" + name.text + "' must be called with '('" + At(Peek().pos));
      }
      p++;

      int args = 0;
      if (Peek().kind != Tok::RPAREN) {
        for (;;) {
          if (!ParseExpression(d + 1)) return false;
          args++;
          if (Peek().kind != Tok::COMMA) break;
          p++;
        }
      }
      if (Peek().kind != Tok::RPAREN) {
        return Fail("missing ')' after the arguments of '" + name.text + "'" + At(Peek().pos));
      }
      p++;

      if (args != fn->arity) {
        return Fail("'" + name.text + "' takes " + std::to_string(fn->arity) + " argument" +
                    (fn->arity == 1 ? "" : "s") + ", got " + std::to_string(args) + At(name.pos));
      }
      // One-argument functions leave the stack where it is; two-argument ones
      // consume an extra slot.
      return Emit(fn->op, 0, 0.f, 1 - fn->arity);
    }
  };

  // ─── runtime helpers ──────────────────────────────────────────────────────
  //
  // Every one of these is branch-and-arithmetic only, and every input C leaves
  // undefined (a zero divisor, INT_MIN / -1, an out-of-range shift count) is
  // answered here. The answers are the ones ./ , .% , .div , .<< and .>>
  // already give, so an expression and the equivalent chain of objects agree.

  const int kIntBits = 32;

  // Signed overflow is undefined in C++, and a patch can feed any pair of ints
  // into an expression — a sanitizer build must not abort on `$i1 * $i2`. The
  // unsigned round trip is the two's-complement wrap every Max patch expects.
  int AddInt(int a, int b) {
    return (int)((unsigned int)a + (unsigned int)b);
  }
  int SubInt(int a, int b) {
    return (int)((unsigned int)a - (unsigned int)b);
  }
  int MulInt(int a, int b) {
    return (int)((unsigned int)a * (unsigned int)b);
  }
  int NegInt(int a) {
    return (int)(0u - (unsigned int)a);
  }

  int IntDivOp(int a, int b) {
    if (b == 0) return 0; // the ./ divide-by-zero convention
    if (a == INT_MIN && b == -1) return INT_MIN; // the true quotient overflows
    return a / b;
  }

  int IntModOp(int a, int b) {
    if (b == 0) return 0;
    if (b == -1) return 0; // exact, and dodges the INT_MIN overflow
    return a % b;
  }

  int ShiftLeftOp(int a, int b) {
    if (b <= 0) return a;
    if (b >= kIntBits) return 0;
    // Through unsigned: shifting into or past the sign bit of a signed int is
    // UB, while the unsigned round trip is the two's-complement answer.
    return (int)((unsigned int)a << b);
  }

  int ShiftRightOp(int a, int b) {
    if (b <= 0) return a;
    if (b >= kIntBits) return a < 0 ? -1 : 0;
    return a >> b;
  }

  float FactOp(float x) {
    const int n = ExprToInt(x);
    if (n < 0) return 0.f; // undefined; 0 rather than a NaN
    // 34! is 2.95e38 and still a float; 35! is not. Beyond that hand back an
    // infinity and let the caller's finite guard turn it into 0.
    if (n > 34) return HUGE_VALF;
    double r = 1.0;
    for (int k = 2; k <= n; k++)
      r *= (double)k;
    return (float)r;
  }

  // The stack effect of each opcode, read off the opcode alone. Evaluate()
  // performs no bounds check per instruction — its safety rests entirely on
  // ExprProgram::stackNeeded being a true high-water mark — so the emitted
  // program is re-walked with this table once, at the end of Compile(). The
  // parser's own accounting comes from the grammar rather than from here, so
  // the two are genuinely independent: a future opcode whose effect is wrong
  // in one of the three places (emit, verify, evaluate) is caught on the
  // control thread instead of overrunning a fixed array at render time.
  int StackEffect(ExprOp op) {
    switch (op) {
    case ExprOp::PUSH_INT:
    case ExprOp::PUSH_FLOAT:
    case ExprOp::PUSH_VAR_INT:
    case ExprOp::PUSH_VAR_FLOAT:
      return 1;

    // Unary operators and the one-argument functions replace their operand.
    case ExprOp::NEG:
    case ExprOp::NOT:
    case ExprOp::BITNOT:
    case ExprOp::F_ABS:
    case ExprOp::F_INT:
    case ExprOp::F_FLOAT:
    case ExprOp::F_SQRT:
    case ExprOp::F_EXP:
    case ExprOp::F_LN:
    case ExprOp::F_LOG10:
    case ExprOp::F_SIN:
    case ExprOp::F_COS:
    case ExprOp::F_TAN:
    case ExprOp::F_ASIN:
    case ExprOp::F_ACOS:
    case ExprOp::F_ATAN:
    case ExprOp::F_SINH:
    case ExprOp::F_COSH:
    case ExprOp::F_TANH:
    case ExprOp::F_ROUND:
    case ExprOp::F_FLOOR:
    case ExprOp::F_CEIL:
    case ExprOp::F_FACT:
      return 0;

    // Everything else consumes two operands and leaves one.
    default:
      return -1;
    }
  }

} // namespace

// ─── ExprProgram ────────────────────────────────────────────────────────────

void ExprProgram::Clear() {
  code.clear();
  error.clear();
  usedInlets = 0;
  inletCount = 1;
  stackNeeded = 0;
  valid = false;
}

bool ExprProgram::Compile(const std::string& source) {
  Clear();

  Compiler c;
  c.code = &code;

  if (!c.Tokenize(source)) {
    code.clear();
    error = c.error;
    return false;
  }
  if (c.tokens.size() == 1) { // only the END token
    code.clear();
    error = "expression is empty";
    return false;
  }
  if (!c.ParseExpression(0)) {
    code.clear();
    error = c.error;
    return false;
  }
  if (c.Peek().kind != Tok::END) {
    code.clear();
    error = "unexpected trailing input" + At(c.Peek().pos);
    return false;
  }
  // Independent verification of the one invariant Evaluate() relies on: walk
  // the emitted program with the per-opcode stack-effect table and confirm the
  // high-water mark the parser measured, that the walk never underflows, and
  // that it ends with exactly one value. Unreachable with the grammar above —
  // which is the point: it is the guard that keeps a future opcode from
  // turning a parser bug into a buffer overrun on the audio thread.
  int simulated = 0;
  int simulatedMax = 0;
  for (const ExprInstr& in : code) {
    simulated += StackEffect(in.op);
    if (simulated < 1) break; // underflow (or a spent operand)
    if (simulated > simulatedMax) simulatedMax = simulated;
  }
  if (simulated != 1 || c.depth != 1 || simulatedMax != c.maxDepth ||
      simulatedMax > kExprMaxStack) {
    code.clear();
    error = "internal error: the expression does not reduce to a single value";
    return false;
  }

  usedInlets = c.usedInlets;
  inletCount = (c.maxVar >= 0) ? c.maxVar + 1 : 1;
  stackNeeded = simulatedMax;
  valid = true;
  return true;
}

ExprValue ExprProgram::Evaluate(const float* vars) const {
  // Both guards are O(1) and keep the walk below free of bounds checks:
  // stackNeeded is the exact high-water mark the compiler measured, and it
  // refused anything above kExprMaxStack.
  if (!valid || code.empty() || vars == nullptr) return ExprValue::Int(0);
  if (stackNeeded > kExprMaxStack) return ExprValue::Int(0);

  ExprValue stack[kExprMaxStack];
  int sp = 0;

  const ExprInstr* ip = code.data();
  const std::size_t n = code.size();

  for (std::size_t k = 0; k < n; k++) {
    const ExprInstr& in = ip[k];
    switch (in.op) {

    // ── operands ──
    case ExprOp::PUSH_INT:
      stack[sp++] = ExprValue::Int(in.i);
      break;
    case ExprOp::PUSH_FLOAT:
      stack[sp++] = ExprValue::Float(in.f);
      break;
    case ExprOp::PUSH_VAR_INT:
      stack[sp++] = ExprValue::Int(ExprToInt(vars[in.i]));
      break;
    case ExprOp::PUSH_VAR_FLOAT:
      stack[sp++] = ExprValue::Float(vars[in.i]);
      break;

    // ── unary ──
    case ExprOp::NEG: {
      ExprValue& a = stack[sp - 1];
      a = a.isInt ? ExprValue::Int(NegInt(a.i)) : ExprValue::Float(-a.f);
      break;
    }
    case ExprOp::NOT: {
      ExprValue& a = stack[sp - 1];
      const bool zero = a.isInt ? (a.i == 0) : (a.f == 0.f);
      a = ExprValue::Int(zero ? 1 : 0);
      break;
    }
    case ExprOp::BITNOT: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(~a.AsInt());
      break;
    }

    // ── arithmetic: int when both operands are ints, as in C ──
    case ExprOp::ADD: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = (a.isInt && b.isInt) ? ExprValue::Int(AddInt(a.i, b.i))
                               : ExprValue::Float(a.AsFloat() + b.AsFloat());
      break;
    }
    case ExprOp::SUB: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = (a.isInt && b.isInt) ? ExprValue::Int(SubInt(a.i, b.i))
                               : ExprValue::Float(a.AsFloat() - b.AsFloat());
      break;
    }
    case ExprOp::MUL: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = (a.isInt && b.isInt) ? ExprValue::Int(MulInt(a.i, b.i))
                               : ExprValue::Float(a.AsFloat() * b.AsFloat());
      break;
    }
    case ExprOp::DIV: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      if (a.isInt && b.isInt) {
        a = ExprValue::Int(IntDivOp(a.i, b.i));
      } else {
        const float d = b.AsFloat();
        a = ExprValue::Float(d == 0.f ? 0.f : a.AsFloat() / d);
      }
      break;
    }
    case ExprOp::MOD: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      if (a.isInt && b.isInt) {
        a = ExprValue::Int(IntModOp(a.i, b.i));
      } else {
        const float d = b.AsFloat();
        a = ExprValue::Float(d == 0.f ? 0.f : std::fmod(a.AsFloat(), d));
      }
      break;
    }

    // ── bitwise / shifts: always int ──
    case ExprOp::SHL: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(ShiftLeftOp(a.AsInt(), b.AsInt()));
      break;
    }
    case ExprOp::SHR: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(ShiftRightOp(a.AsInt(), b.AsInt()));
      break;
    }
    case ExprOp::BITAND: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(a.AsInt() & b.AsInt());
      break;
    }
    case ExprOp::BITXOR: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(a.AsInt() ^ b.AsInt());
      break;
    }
    case ExprOp::BITOR: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(a.AsInt() | b.AsInt());
      break;
    }

    // ── comparison / logic: always int ──
    case ExprOp::LT: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i < b.i) : (a.AsFloat() < b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::LE: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i <= b.i) : (a.AsFloat() <= b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::GT: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i > b.i) : (a.AsFloat() > b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::GE: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i >= b.i) : (a.AsFloat() >= b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::EQ: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i == b.i) : (a.AsFloat() == b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::NE: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool r = (a.isInt && b.isInt) ? (a.i != b.i) : (a.AsFloat() != b.AsFloat());
      a = ExprValue::Int(r ? 1 : 0);
      break;
    }
    case ExprOp::AND: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool l = a.isInt ? (a.i != 0) : (a.f != 0.f);
      const bool r = b.isInt ? (b.i != 0) : (b.f != 0.f);
      a = ExprValue::Int((l && r) ? 1 : 0);
      break;
    }
    case ExprOp::OR: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      const bool l = a.isInt ? (a.i != 0) : (a.f != 0.f);
      const bool r = b.isInt ? (b.i != 0) : (b.f != 0.f);
      a = ExprValue::Int((l || r) ? 1 : 0);
      break;
    }

    // ── conversions and type-preserving functions ──
    case ExprOp::F_INT: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Int(a.AsInt());
      break;
    }
    case ExprOp::F_FLOAT: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(a.AsFloat());
      break;
    }
    case ExprOp::F_ABS: {
      ExprValue& a = stack[sp - 1];
      if (a.isInt) {
        // abs(INT_MIN) has no int answer; INT_MIN is the two's-complement one.
        a = ExprValue::Int(a.i == INT_MIN ? INT_MIN : (a.i < 0 ? -a.i : a.i));
      } else {
        a = ExprValue::Float(std::fabs(a.f));
      }
      break;
    }
    case ExprOp::F_MIN: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      if (a.isInt && b.isInt) {
        a = ExprValue::Int(a.i < b.i ? a.i : b.i);
      } else {
        const float x = a.AsFloat();
        const float y = b.AsFloat();
        a = ExprValue::Float(x < y ? x : y);
      }
      break;
    }
    case ExprOp::F_MAX: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      if (a.isInt && b.isInt) {
        a = ExprValue::Int(a.i > b.i ? a.i : b.i);
      } else {
        const float x = a.AsFloat();
        const float y = b.AsFloat();
        a = ExprValue::Float(x > y ? x : y);
      }
      break;
    }

    // ── float-valued functions ──
    case ExprOp::F_SQRT: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::sqrt(a.AsFloat()));
      break;
    }
    case ExprOp::F_EXP: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::exp(a.AsFloat()));
      break;
    }
    case ExprOp::F_LN: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::log(a.AsFloat()));
      break;
    }
    case ExprOp::F_LOG10: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::log10(a.AsFloat()));
      break;
    }
    case ExprOp::F_SIN: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::sin(a.AsFloat()));
      break;
    }
    case ExprOp::F_COS: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::cos(a.AsFloat()));
      break;
    }
    case ExprOp::F_TAN: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::tan(a.AsFloat()));
      break;
    }
    case ExprOp::F_ASIN: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::asin(a.AsFloat()));
      break;
    }
    case ExprOp::F_ACOS: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::acos(a.AsFloat()));
      break;
    }
    case ExprOp::F_ATAN: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::atan(a.AsFloat()));
      break;
    }
    case ExprOp::F_SINH: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::sinh(a.AsFloat()));
      break;
    }
    case ExprOp::F_COSH: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::cosh(a.AsFloat()));
      break;
    }
    case ExprOp::F_TANH: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::tanh(a.AsFloat()));
      break;
    }
    case ExprOp::F_ROUND: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::round(a.AsFloat()));
      break;
    }
    case ExprOp::F_FLOOR: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::floor(a.AsFloat()));
      break;
    }
    case ExprOp::F_CEIL: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::ceil(a.AsFloat()));
      break;
    }
    case ExprOp::F_FACT: {
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(FactOp(a.AsFloat()));
      break;
    }
    case ExprOp::F_POW: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::pow(a.AsFloat(), b.AsFloat()));
      break;
    }
    case ExprOp::F_ATAN2: {
      const ExprValue b = stack[--sp];
      ExprValue& a = stack[sp - 1];
      a = ExprValue::Float(std::atan2(a.AsFloat(), b.AsFloat()));
      break;
    }
    }
  }

  ExprValue result = stack[0];
  // The family convention: a NaN or an infinity leaves as 0 rather than
  // poisoning everything downstream. Reachable from 1./0., sqrt(-1), an
  // overflowing pow, or simply a non-finite value arriving on an inlet.
  if (!result.isInt && !std::isfinite(result.f)) result = ExprValue::Float(0.f);
  return result;
}

// ─── list parsing ───────────────────────────────────────────────────────────

int YSE::PATCHER::ExprParseFloatList(const char* text, float* out, int cap) {
  if (text == nullptr || out == nullptr || cap <= 0) return 0;

  int count = 0;
  const char* s = text;
  while (*s != '\0' && count < cap) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
      s++;
    if (*s == '\0') break;

    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s) {
      // Not a number: skip the whole token rather than spinning on it. Max
      // ignores non-numeric list items too.
      while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r')
        s++;
      continue;
    }
    out[count++] = std::isfinite(v) ? v : 0.f;
    s = end;
  }
  return count;
}

// ─── number formatting ──────────────────────────────────────────────────────

namespace {

  // Exact powers of ten. Every value up to 1e22 is representable in a double
  // without rounding, which is what makes Pow10 exact over the range a patcher
  // realistically produces; beyond it the composed products drift by an ulp or
  // two of *double*, which is still some twenty-five bits below the float
  // precision the round-trip test below actually decides on.
  const double kPow10[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                             1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};

  double Pow10(int e) {
    const bool negative = e < 0;
    if (negative) e = -e;
    double r = 1.0;
    while (e > 22) {
      r *= kPow10[22];
      e -= 22;
    }
    r *= kPow10[e];
    return negative ? 1.0 / r : r;
  }

  // Integer powers of ten, for the digit-trimming loop. 10^9 fits an int64
  // comfortably.
  const long long kPow10i[10] = {1LL,      10LL,      100LL,      1000LL,      10000LL,
                                 100000LL, 1000000LL, 10000000LL, 100000000LL, 1000000000LL};

  // Write |value|'s digits into out; returns the length. Handles INT_MIN,
  // which is why the magnitude is taken in unsigned arithmetic.
  int FormatInt(int value, char* out) {
    unsigned int u = value < 0 ? (0u - (unsigned int)value) : (unsigned int)value;
    char digits[12];
    int n = 0;
    do {
      digits[n++] = (char)('0' + (int)(u % 10u));
      u /= 10u;
    } while (u != 0u);

    int len = 0;
    if (value < 0) out[len++] = '-';
    while (n > 0)
      out[len++] = digits[--n];
    return len;
  }

} // namespace

int YSE::PATCHER::ExprFormatValue(const ExprValue& value, char* out, int cap) {
  if (out == nullptr || cap < 2) {
    if (out != nullptr && cap > 0) out[0] = '\0';
    return 0;
  }
  if (cap < kExprValueTextMax) {
    out[0] = '\0';
    return 0;
  }

  if (value.isInt) {
    const int len = FormatInt(value.i, out);
    out[len] = '\0';
    return len;
  }

  const float v = value.f;
  // Both answered with the float spelling of zero: "0." is still a float to
  // whatever parses this list back, and a non-finite value is the 0 the rest
  // of the math family substitutes.
  if (!std::isfinite(v) || v == 0.f) {
    out[0] = '0';
    out[1] = '.';
    out[2] = '\0';
    return 2;
  }

  const double a = std::fabs((double)v);

  // Normalise to a nine-digit decimal mantissa: a ≈ m9 * 10^(e10 - 8), with
  // m9 in [1e8, 1e9). log10 gets the exponent right to within one either way,
  // so the loop only ever corrects by a single step.
  int e10 = (int)std::floor(std::log10(a));
  for (int guard = 0; guard < 4; guard++) {
    const double scaled = a / Pow10(e10 - 8);
    if (scaled >= 1e9) {
      e10++;
      continue;
    }
    if (scaled < 1e8) {
      e10--;
      continue;
    }
    break;
  }
  long long m9 = (long long)std::llround(a / Pow10(e10 - 8));
  if (m9 >= 1000000000LL) {
    m9 /= 10;
    e10++;
  }
  if (m9 < 100000000LL) m9 = 100000000LL; // unreachable in practice; keeps the width

  // Shortest round trip: the fewest significant digits that read back as the
  // same float. Nine always works — it is the round-trip width of a float —
  // so the loop always terminates with a usable answer.
  long long mant = m9;
  int digits = 9;
  int exp10 = e10 - 8; // value == mant * 10^exp10
  for (int p = 1; p <= 9; p++) {
    const long long div = kPow10i[9 - p];
    long long m = (m9 + div / 2) / div;
    int e = (e10 - 8) + (9 - p);
    if (m >= kPow10i[p]) { // the rounding carried, e.g. 999 -> 1000
      m /= 10;
      e++;
    }
    if ((float)((double)m * Pow10(e)) == (float)a) {
      mant = m;
      digits = p;
      exp10 = e;
      break;
    }
  }

  // Split the mantissa into characters, most significant first.
  char digitChars[10];
  for (int i = digits - 1; i >= 0; i--) {
    digitChars[i] = (char)('0' + (int)(mant % 10));
    mant /= 10;
  }

  int len = 0;
  if (v < 0.f) out[len++] = '-';

  // Digits before the decimal point if this were written in full.
  const int point = digits + exp10;

  if (point > -4 && point <= 9) {
    // Fixed point — the range a patcher actually works in.
    if (point <= 0) {
      out[len++] = '0';
      out[len++] = '.';
      for (int i = 0; i < -point; i++)
        out[len++] = '0';
      for (int i = 0; i < digits; i++)
        out[len++] = digitChars[i];
    } else if (point >= digits) {
      for (int i = 0; i < digits; i++)
        out[len++] = digitChars[i];
      for (int i = 0; i < point - digits; i++)
        out[len++] = '0';
      // The trailing point is what keeps a whole-numbered float readable as a
      // float rather than as an int.
      out[len++] = '.';
    } else {
      for (int i = 0; i < point; i++)
        out[len++] = digitChars[i];
      out[len++] = '.';
      for (int i = point; i < digits; i++)
        out[len++] = digitChars[i];
    }
  } else {
    // Exponent form; the 'e' marks it as a float on its own.
    out[len++] = digitChars[0];
    if (digits > 1) {
      out[len++] = '.';
      for (int i = 1; i < digits; i++)
        out[len++] = digitChars[i];
    }
    out[len++] = 'e';
    int e = point - 1;
    if (e < 0) {
      out[len++] = '-';
      e = -e;
    } else {
      out[len++] = '+';
    }
    // Two digits always, as C's %e does; a float's exponent never needs more.
    out[len++] = (char)('0' + (e / 10) % 10);
    out[len++] = (char)('0' + e % 10);
  }

  out[len] = '\0';
  return len;
}

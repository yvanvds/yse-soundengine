#include "gRegexEngine.h"

using namespace YSE::PATCHER;

namespace {

  // The three shorthand sets, shared by `\d`-style escapes inside and outside
  // a character class.
  constexpr int kSetDigit = 0;
  constexpr int kSetWord = 1;
  constexpr int kSetSpace = 2;

  void FillSet(YSE::PATCHER::RegexClass& cls, int setId) {
    cls.Clear();
    switch (setId) {
    case kSetDigit:
      cls.AddRange('0', '9');
      break;
    case kSetWord:
      cls.AddRange('a', 'z');
      cls.AddRange('A', 'Z');
      cls.AddRange('0', '9');
      cls.Add('_');
      break;
    default: // kSetSpace
      cls.Add(' ');
      cls.Add('\t');
      cls.Add('\n');
      cls.Add('\r');
      cls.Add('\f');
      cls.Add('\v');
      break;
    }
  }

} // namespace

namespace YSE {
  namespace PATCHER {

    // ── the parse tree ────────────────────────────────────────────────────────
    //
    // Compile() is a two-pass affair: a recursive-descent parser builds this
    // tree, and a second walk lowers it into the flat instruction array. The
    // detour through a tree is what lets the quantifiers work: `a*` has to wrap
    // code that was already emitted, and rewriting a flat array in place means
    // relocating every jump that crosses the insertion point. With a tree the
    // wrapper simply emits its child wherever it needs it.

    enum class RegexNodeKind : unsigned char {
      Empty, ///< matches the empty string (an empty alternative, `a|`)
      Lit, ///< one literal character
      Cls, ///< one character class
      Any, ///< `.`
      Bol, ///< `^`
      Eol, ///< `$`
      Cat, ///< left then right
      Alt, ///< left or right
      Repeat, ///< left, min..max times, greedy or lazy
      Group, ///< left, capturing into `group` when that is non-zero
    };

    struct RegexNode {
      RegexNodeKind kind = RegexNodeKind::Empty;
      unsigned char ch = 0; ///< Lit
      short cls = -1; ///< Cls: index into RegexProgram::classes
      short left = -1; ///< Cat/Alt/Repeat/Group child
      short right = -1; ///< Cat/Alt second child
      short min = 0; ///< Repeat lower bound
      short max = 0; ///< Repeat upper bound, -1 for unbounded
      short group = 0; ///< Group: 1-9 when capturing, 0 when not
      bool greedy = true; ///< Repeat
    };

    /**
     *  The control-thread half of the engine: parses a pattern into the node
     *  array above and lowers it into the program. Lives here rather than
     *  inside RegexProgram so the RT half of the class stays small and
     *  obviously free of anything that allocates.
     */
    struct RegexCompiler {
      RegexCompiler(RegexProgram& program, const std::string& source) : re(program), text(source) {}

      RegexProgram& re;
      const std::string& text;
      int pos = 0;
      int depth = 0;
      int nodeCount = 0;
      std::string error;
      RegexNode nodes[kRegexMaxNodes];

      int Size() const {
        return (int)text.size();
      }

      // First error wins: the deepest parser frame usually has the most
      // specific complaint, and it is the one that fails first.
      bool Fail(const std::string& what) {
        if (error.empty()) {
          error = what;
          error += " (at character ";
          error += std::to_string(pos);
          error += ")";
        }
        return false;
      }

      short NewNode(RegexNodeKind kind) {
        if (nodeCount >= kRegexMaxNodes) {
          Fail("the pattern has too many elements");
          return -1;
        }
        const short index = (short)nodeCount++;
        nodes[index] = RegexNode();
        nodes[index].kind = kind;
        return index;
      }

      // ── parser ──────────────────────────────────────────────────────────

      bool ParseAlt(short& out);
      bool ParseConcat(short& out);
      bool ParseRepeat(short& out);
      bool ParseAtom(short& out);
      bool ParseClass(short& out);
      bool ParseBounds(short& min, short& max);
      bool ParseEscape(bool& isSet, int& setId, bool& negateSet, unsigned char& ch);

      // Whether @p node can match without consuming a character. Used to
      // refuse `(a*)*`: an unbounded repeat of a nullable body is an infinite
      // loop in a backtracking VM, and saying so at compile time beats letting
      // the step budget run out at message time.
      bool CanBeEmpty(short node) const;

      // ── lowering ────────────────────────────────────────────────────────

      int AddInstr(RegexOp op) {
        if (re.count >= kRegexMaxProgram) {
          Fail("the pattern is too complex");
          return -1;
        }
        const int index = re.count++;
        re.prog[index] = RegexInstr();
        re.prog[index].op = op;
        return index;
      }

      bool Emit(short node);
      bool EmitAlt(short node);
      bool EmitGroup(short node);
      bool EmitRepeat(short node);
    };

    bool RegexCompiler::ParseAlt(short& out) {
      short items[kRegexMaxAlternatives];
      int n = 0;

      short first = -1;
      if (!ParseConcat(first)) return false;
      items[n++] = first;

      while (pos < Size() && text[pos] == '|') {
        pos++;
        if (n >= kRegexMaxAlternatives) return Fail("too many '|' alternatives");
        short next = -1;
        if (!ParseConcat(next)) return false;
        items[n++] = next;
      }

      // Fold right, so EmitAlt can walk the chain with a loop instead of
      // recursing once per alternative.
      short acc = items[n - 1];
      for (int i = n - 2; i >= 0; i--) {
        const short node = NewNode(RegexNodeKind::Alt);
        if (node < 0) return false;
        nodes[node].left = items[i];
        nodes[node].right = acc;
        acc = node;
      }
      out = acc;
      return true;
    }

    bool RegexCompiler::ParseConcat(short& out) {
      // One entry per atom, and an atom is at least one character, so the
      // pattern-length cap bounds this array.
      short items[kRegexMaxPattern];
      int n = 0;

      while (pos < Size() && text[pos] != '|' && text[pos] != ')') {
        short item = -1;
        if (!ParseRepeat(item)) return false;
        if (n >= kRegexMaxPattern) return Fail("the pattern has too many elements");
        items[n++] = item;
      }

      if (n == 0) {
        // An empty alternative — `(a|)` — matches the empty string.
        out = NewNode(RegexNodeKind::Empty);
        return out >= 0;
      }

      short acc = items[n - 1];
      for (int i = n - 2; i >= 0; i--) {
        const short node = NewNode(RegexNodeKind::Cat);
        if (node < 0) return false;
        nodes[node].left = items[i];
        nodes[node].right = acc;
        acc = node;
      }
      out = acc;
      return true;
    }

    bool RegexCompiler::ParseRepeat(short& out) {
      short atom = -1;
      if (!ParseAtom(atom)) return false;

      if (pos >= Size()) {
        out = atom;
        return true;
      }

      short min = 0;
      short max = 0;
      const char c = text[pos];
      if (c == '*') {
        min = 0;
        max = -1;
        pos++;
      } else if (c == '+') {
        min = 1;
        max = -1;
        pos++;
      } else if (c == '?') {
        min = 0;
        max = 1;
        pos++;
      } else if (c == '{') {
        if (!ParseBounds(min, max)) return false;
      } else {
        out = atom;
        return true;
      }

      const RegexNodeKind kind = nodes[atom].kind;
      if (kind == RegexNodeKind::Bol || kind == RegexNodeKind::Eol ||
          kind == RegexNodeKind::Empty) {
        return Fail("there is nothing to repeat here");
      }
      if (max < 0 && CanBeEmpty(atom)) {
        return Fail("the body of an unbounded repeat can match the empty string, which would "
                    "never terminate");
      }

      bool greedy = true;
      if (pos < Size() && text[pos] == '?') {
        greedy = false;
        pos++;
      } else if (pos < Size() && text[pos] == '+') {
        return Fail("possessive quantifiers are not supported");
      }

      const short node = NewNode(RegexNodeKind::Repeat);
      if (node < 0) return false;
      nodes[node].left = atom;
      nodes[node].min = min;
      nodes[node].max = max;
      nodes[node].greedy = greedy;
      out = node;
      return true;
    }

    bool RegexCompiler::ParseBounds(short& min, short& max) {
      // text[pos] == '{'
      pos++;
      if (pos >= Size() || text[pos] < '0' || text[pos] > '9') {
        return Fail("'{' must be followed by a repeat count; write \\{ for a literal brace");
      }
      int low = 0;
      while (pos < Size() && text[pos] >= '0' && text[pos] <= '9') {
        low = low * 10 + (text[pos] - '0');
        if (low > kRegexMaxRepeat) return Fail("the repeat count is too large");
        pos++;
      }

      int high = low;
      if (pos < Size() && text[pos] == ',') {
        pos++;
        if (pos < Size() && text[pos] == '}') {
          high = -1; // {n,}
        } else {
          high = 0;
          if (pos >= Size() || text[pos] < '0' || text[pos] > '9') {
            return Fail("expected a repeat count after ','");
          }
          while (pos < Size() && text[pos] >= '0' && text[pos] <= '9') {
            high = high * 10 + (text[pos] - '0');
            if (high > kRegexMaxRepeat) return Fail("the repeat count is too large");
            pos++;
          }
        }
      }

      if (pos >= Size() || text[pos] != '}') return Fail("unterminated '{' repeat count");
      pos++;

      if (high >= 0 && high < low) return Fail("the repeat count counts down rather than up");
      min = (short)low;
      max = (short)high;
      return true;
    }

    bool RegexCompiler::ParseEscape(bool& isSet, int& setId, bool& negateSet, unsigned char& ch) {
      isSet = false;
      setId = kSetDigit;
      negateSet = false;
      ch = 0;

      if (pos >= Size()) return Fail("the pattern ends with a lone backslash");
      const char c = text[pos++];

      switch (c) {
      case 'd':
      case 'D':
        isSet = true;
        setId = kSetDigit;
        negateSet = (c == 'D');
        return true;
      case 'w':
      case 'W':
        isSet = true;
        setId = kSetWord;
        negateSet = (c == 'W');
        return true;
      case 's':
      case 'S':
        isSet = true;
        setId = kSetSpace;
        negateSet = (c == 'S');
        return true;
      case 'n':
        ch = '\n';
        return true;
      case 't':
        ch = '\t';
        return true;
      case 'r':
        ch = '\r';
        return true;
      case 'f':
        ch = '\f';
        return true;
      case 'v':
        ch = '\v';
        return true;
      case 'x': {
        // The way to write a byte the patcher's parameter tokenizer would eat
        // before the pattern ever got here — a literal space is \x20.
        int value = 0;
        int digits = 0;
        while (digits < 2 && pos < Size()) {
          const char hex = text[pos];
          int digit = 0;
          if (hex >= '0' && hex <= '9') {
            digit = hex - '0';
          } else if (hex >= 'a' && hex <= 'f') {
            digit = hex - 'a' + 10;
          } else if (hex >= 'A' && hex <= 'F') {
            digit = hex - 'A' + 10;
          } else {
            break;
          }
          value = value * 16 + digit;
          pos++;
          digits++;
        }
        if (digits != 2) return Fail("\\x needs exactly two hexadecimal digits");
        ch = (unsigned char)value;
        return true;
      }
      case 'b':
      case 'B':
        return Fail("\\b and \\B (word boundaries) are not supported");
      case 'A':
      case 'Z':
      case 'z':
        return Fail("\\A, \\Z and \\z are not supported; use ^ and $");
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        return Fail("backreferences are not supported");
      case '\\':
      case '.':
      case '*':
      case '+':
      case '?':
      case '(':
      case ')':
      case '[':
      case ']':
      case '{':
      case '}':
      case '|':
      case '^':
      case '$':
      case '-':
      case '/':
      case '%':
      case ' ':
        ch = (unsigned char)c;
        return true;
      default:
        break;
      }
      pos--; // point the offset at the offending character
      return Fail(std::string("unknown escape '\\") + c + "'");
    }

    bool RegexCompiler::ParseClass(short& out) {
      // text[pos] == '['
      pos++;
      bool negate = false;
      if (pos < Size() && text[pos] == '^') {
        negate = true;
        pos++;
      }

      if (re.classCount >= kRegexMaxClasses) {
        return Fail("the pattern has too many character classes");
      }

      RegexClass cls;
      cls.Clear();
      bool empty = true;

      while (pos < Size() && text[pos] != ']') {
        unsigned char low = 0;
        bool isSet = false;
        int setId = 0;
        bool negateSet = false;

        if (text[pos] == '\\') {
          pos++;
          if (!ParseEscape(isSet, setId, negateSet, low)) return false;
          if (isSet) {
            RegexClass shorthand;
            FillSet(shorthand, setId);
            if (negateSet) shorthand.Negate();
            cls.Merge(shorthand);
            empty = false;
            continue;
          }
        } else {
          low = (unsigned char)text[pos++];
        }

        if (pos + 1 < Size() && text[pos] == '-' && text[pos + 1] != ']') {
          pos++; // the '-'
          unsigned char high = 0;
          if (text[pos] == '\\') {
            pos++;
            if (!ParseEscape(isSet, setId, negateSet, high)) return false;
            if (isSet) return Fail("a \\d-style shorthand cannot end a range");
          } else {
            high = (unsigned char)text[pos++];
          }
          if (high < low) return Fail("this range in a character class counts down");
          cls.AddRange(low, high);
        } else {
          cls.Add(low);
        }
        empty = false;
      }

      if (pos >= Size()) return Fail("unterminated character class: no closing ']'");
      pos++; // the ']'
      if (empty) return Fail("empty character class");

      if (negate) cls.Negate();
      re.classes[re.classCount] = cls;

      const short node = NewNode(RegexNodeKind::Cls);
      if (node < 0) return false;
      nodes[node].cls = (short)re.classCount;
      re.classCount++;
      out = node;
      return true;
    }

    bool RegexCompiler::ParseAtom(short& out) {
      if (pos >= Size()) return Fail("the pattern ends where an element was expected");

      const char c = text[pos];
      switch (c) {
      case '(': {
        pos++;
        short group = 0;
        if (pos + 1 < Size() && text[pos] == '?' && text[pos + 1] == ':') {
          pos += 2; // non-capturing
        } else if (pos < Size() && text[pos] == '?') {
          return Fail("(?...) constructs are not supported; only (?: ) for a non-capturing group");
        } else {
          if (re.groupCount >= kRegexMaxGroups) {
            return Fail("more than nine capturing groups; a substitution can only name %1-%9");
          }
          re.groupCount++;
          group = (short)re.groupCount;
        }

        depth++;
        if (depth > kRegexMaxNesting) return Fail("groups are nested too deeply");
        short inner = -1;
        if (!ParseAlt(inner)) return false;
        depth--;

        if (pos >= Size() || text[pos] != ')') return Fail("unmatched '('");
        pos++;

        const short node = NewNode(RegexNodeKind::Group);
        if (node < 0) return false;
        nodes[node].left = inner;
        nodes[node].group = group;
        out = node;
        return true;
      }
      case ')':
        return Fail("unmatched ')'");
      case '[':
        return ParseClass(out);
      case '.':
        pos++;
        out = NewNode(RegexNodeKind::Any);
        return out >= 0;
      case '^':
        pos++;
        out = NewNode(RegexNodeKind::Bol);
        return out >= 0;
      case '$':
        pos++;
        out = NewNode(RegexNodeKind::Eol);
        return out >= 0;
      case '*':
      case '+':
      case '?':
        return Fail("there is nothing to repeat here");
      case '{':
        return Fail("'{' has nothing to repeat; write \\{ for a literal brace");
      case '\\': {
        pos++;
        bool isSet = false;
        int setId = 0;
        bool negateSet = false;
        unsigned char ch = 0;
        if (!ParseEscape(isSet, setId, negateSet, ch)) return false;
        if (isSet) {
          if (re.classCount >= kRegexMaxClasses) {
            return Fail("the pattern has too many character classes");
          }
          RegexClass shorthand;
          FillSet(shorthand, setId);
          if (negateSet) shorthand.Negate();
          re.classes[re.classCount] = shorthand;
          const short node = NewNode(RegexNodeKind::Cls);
          if (node < 0) return false;
          nodes[node].cls = (short)re.classCount;
          re.classCount++;
          out = node;
          return true;
        }
        const short node = NewNode(RegexNodeKind::Lit);
        if (node < 0) return false;
        nodes[node].ch = ch;
        out = node;
        return true;
      }
      default: {
        pos++;
        const short node = NewNode(RegexNodeKind::Lit);
        if (node < 0) return false;
        nodes[node].ch = (unsigned char)c;
        out = node;
        return true;
      }
      }
    }

    bool RegexCompiler::CanBeEmpty(short node) const {
      if (node < 0) return true;
      switch (nodes[node].kind) {
      case RegexNodeKind::Empty:
      case RegexNodeKind::Bol:
      case RegexNodeKind::Eol:
        return true;
      case RegexNodeKind::Lit:
      case RegexNodeKind::Cls:
      case RegexNodeKind::Any:
        return false;
      case RegexNodeKind::Cat:
        return CanBeEmpty(nodes[node].left) && CanBeEmpty(nodes[node].right);
      case RegexNodeKind::Alt:
        return CanBeEmpty(nodes[node].left) || CanBeEmpty(nodes[node].right);
      case RegexNodeKind::Group:
        return CanBeEmpty(nodes[node].left);
      case RegexNodeKind::Repeat:
        return nodes[node].min == 0 || CanBeEmpty(nodes[node].left);
      }
      return true;
    }

    bool RegexCompiler::Emit(short node) {
      for (;;) {
        if (node < 0) return true;
        const RegexNode& n = nodes[node];
        switch (n.kind) {
        case RegexNodeKind::Empty:
          return true;
        case RegexNodeKind::Lit: {
          const int i = AddInstr(RegexOp::Char);
          if (i < 0) return false;
          re.prog[i].ch = n.ch;
          return true;
        }
        case RegexNodeKind::Cls: {
          const int i = AddInstr(RegexOp::Class);
          if (i < 0) return false;
          re.prog[i].a = n.cls;
          return true;
        }
        case RegexNodeKind::Any:
          return AddInstr(RegexOp::Any) >= 0;
        case RegexNodeKind::Bol:
          return AddInstr(RegexOp::Bol) >= 0;
        case RegexNodeKind::Eol:
          return AddInstr(RegexOp::Eol) >= 0;
        case RegexNodeKind::Cat:
          // Walk the chain rather than recursing into it: a concatenation is
          // as long as the pattern, and this keeps the emitter's own stack
          // depth tied to nesting instead.
          if (!Emit(n.left)) return false;
          node = n.right;
          continue;
        case RegexNodeKind::Alt:
          return EmitAlt(node);
        case RegexNodeKind::Group:
          return EmitGroup(node);
        case RegexNodeKind::Repeat:
          return EmitRepeat(node);
        }
        return true;
      }
    }

    bool RegexCompiler::EmitAlt(short node) {
      //   Split L1, L2
      // L1: <left>  Jump END
      // L2: <the rest of the chain>
      // END:
      int jumps[kRegexMaxAlternatives];
      int jumpCount = 0;

      short cur = node;
      while (cur >= 0 && nodes[cur].kind == RegexNodeKind::Alt) {
        const int split = AddInstr(RegexOp::Split);
        if (split < 0) return false;
        re.prog[split].a = (short)re.count;
        if (!Emit(nodes[cur].left)) return false;
        const int jump = AddInstr(RegexOp::Jump);
        if (jump < 0) return false;
        if (jumpCount >= kRegexMaxAlternatives) return Fail("too many '|' alternatives");
        jumps[jumpCount++] = jump;
        re.prog[split].b = (short)re.count;
        cur = nodes[cur].right;
      }
      if (!Emit(cur)) return false;
      for (int i = 0; i < jumpCount; i++) {
        re.prog[jumps[i]].a = (short)re.count;
      }
      return true;
    }

    bool RegexCompiler::EmitGroup(short node) {
      const short group = nodes[node].group;
      if (group > 0) {
        const int i = AddInstr(RegexOp::Save);
        if (i < 0) return false;
        re.prog[i].a = (short)(2 * group);
      }
      if (!Emit(nodes[node].left)) return false;
      if (group > 0) {
        const int i = AddInstr(RegexOp::Save);
        if (i < 0) return false;
        re.prog[i].a = (short)(2 * group + 1);
      }
      return true;
    }

    bool RegexCompiler::EmitRepeat(short node) {
      const short child = nodes[node].left;
      const int min = nodes[node].min;
      const int max = nodes[node].max;
      const bool greedy = nodes[node].greedy;

      for (int i = 0; i < min; i++) {
        if (!Emit(child)) return false;
      }

      if (max < 0) {
        // L1: Split BODY, END   (greedy) / Split END, BODY (lazy)
        // BODY: <child> Jump L1
        // END:
        const int loop = re.count;
        const int split = AddInstr(RegexOp::Split);
        if (split < 0) return false;
        const int body = re.count;
        if (!Emit(child)) return false;
        const int jump = AddInstr(RegexOp::Jump);
        if (jump < 0) return false;
        re.prog[jump].a = (short)loop;
        const int end = re.count;
        re.prog[split].a = (short)(greedy ? body : end);
        re.prog[split].b = (short)(greedy ? end : body);
        return true;
      }

      // `a{2,4}` is `a a (a (a)?)?`: the optionals nest, so the second one is
      // only reachable once the first was taken and the leftmost-first order
      // is the one PCRE gives.
      const int optional = max - min;
      int splits[kRegexMaxRepeat];
      int splitCount = 0;
      for (int i = 0; i < optional; i++) {
        const int split = AddInstr(RegexOp::Split);
        if (split < 0) return false;
        if (splitCount >= kRegexMaxRepeat) return Fail("the repeat count is too large");
        splits[splitCount++] = split;
        if (!Emit(child)) return false;
      }
      const int end = re.count;
      for (int i = 0; i < splitCount; i++) {
        const int split = splits[i];
        const int body = split + 1; // the child is emitted right after its split
        re.prog[split].a = (short)(greedy ? body : end);
        re.prog[split].b = (short)(greedy ? end : body);
      }
      return true;
    }

    // ── RegexProgram ──────────────────────────────────────────────────────────

    void RegexProgram::Clear() {
      count = 0;
      classCount = 0;
      groupCount = 0;
      valid = false;
      error.clear();
      for (RegexClass& cls : classes) {
        cls.Clear();
      }
    }

    bool RegexProgram::Compile(const std::string& pattern) {
      Clear();

      std::string failure;
      if (pattern.empty()) {
        failure = "the pattern is empty";
      } else if ((int)pattern.size() > kRegexMaxPattern) {
        failure = "the pattern is longer than " + std::to_string(kRegexMaxPattern) + " characters";
      } else {
        RegexCompiler compiler(*this, pattern);
        short root = -1;
        if (!compiler.ParseAlt(root)) {
          failure = compiler.error;
        } else if (compiler.pos != (int)pattern.size()) {
          // ParseAlt stops at a ')' it does not own.
          compiler.Fail("unmatched ')'");
          failure = compiler.error;
        } else {
          // Slots 0 and 1 hold the whole match; the groups fill 2..19.
          const int open = compiler.AddInstr(RegexOp::Save);
          if (open >= 0) prog[open].a = 0;
          const bool emitted = open >= 0 && compiler.Emit(root);
          const int close = emitted ? compiler.AddInstr(RegexOp::Save) : -1;
          if (close >= 0) prog[close].a = 1;
          const int done = close >= 0 ? compiler.AddInstr(RegexOp::Match) : -1;
          if (done < 0) {
            failure = compiler.error.empty() ? "the pattern is too complex" : compiler.error;
          }
        }
      }

      if (!failure.empty()) {
        Clear();
        error = failure;
        return false;
      }
      valid = true;
      return true;
    }

    bool RegexProgram::RunAt(const char* text, int length, int start, int* slots,
                             int& budget) const {
      // One pending alternative: where to resume, how much of the subject was
      // consumed, and how far to unwind the capture log.
      struct BtEntry {
        int pc;
        int sp;
        int undo;
      };
      // A capture write, so backtracking can put the old value back without
      // copying the whole slot array per branch.
      struct UndoEntry {
        int slot;
        int value;
      };

      BtEntry stack[kRegexMaxBacktrack];
      UndoEntry undo[kRegexMaxUndo];
      int stackCount = 0;
      int undoCount = 0;

      int pc = 0;
      int sp = start;

      for (;;) {
        if (budget <= 0) return false;
        budget--;

        bool ok = false;
        const RegexInstr& in = prog[pc];
        switch (in.op) {
        case RegexOp::Char:
          if (sp < length && (unsigned char)text[sp] == in.ch) {
            sp++;
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Class:
          if (sp < length && classes[in.a].Test((unsigned char)text[sp])) {
            sp++;
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Any:
          if (sp < length) {
            sp++;
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Bol:
          if (sp == 0) {
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Eol:
          if (sp == length) {
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Save:
          // Running out of undo room fails the branch rather than growing
          // anything: a bounded wrong answer beats an unbounded right one on
          // this thread.
          if (undoCount < kRegexMaxUndo) {
            undo[undoCount].slot = in.a;
            undo[undoCount].value = slots[in.a];
            undoCount++;
            slots[in.a] = sp;
            pc++;
            ok = true;
          }
          break;
        case RegexOp::Split:
          if (stackCount < kRegexMaxBacktrack) {
            stack[stackCount].pc = in.b;
            stack[stackCount].sp = sp;
            stack[stackCount].undo = undoCount;
            stackCount++;
            pc = in.a;
            ok = true;
          }
          break;
        case RegexOp::Jump:
          pc = in.a;
          ok = true;
          break;
        case RegexOp::Match:
          return true;
        }

        if (ok) continue;
        if (stackCount == 0) return false;

        stackCount--;
        pc = stack[stackCount].pc;
        sp = stack[stackCount].sp;
        while (undoCount > stack[stackCount].undo) {
          undoCount--;
          slots[undo[undoCount].slot] = undo[undoCount].value;
        }
      }
    }

    bool RegexProgram::Search(const char* text, int length, int from, RegexMatch& out,
                              int& budget) const {
      if (!valid || text == nullptr || length < 0) return false;
      if (from < 0) from = 0;

      int slots[kRegexMaxSlots];
      for (int start = from; start <= length; start++) {
        if (budget <= 0) return false;
        for (int& slot : slots) {
          slot = -1;
        }
        if (!RunAt(text, length, start, slots, budget)) continue;

        out.Clear();
        for (int group = 0; group <= kRegexMaxGroups; group++) {
          out.begin[group] = slots[2 * group];
          out.end[group] = slots[2 * group + 1];
        }
        return true;
      }
      return false;
    }

  } // namespace PATCHER
} // namespace YSE

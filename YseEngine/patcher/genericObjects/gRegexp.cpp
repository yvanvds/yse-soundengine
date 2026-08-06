#include "gRegexp.h"
#include "../pObjectList.hpp"
#include "../math/gExprEval.h"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;

#define className gRegexp

CONSTRUCT() {
  ADD_IN_0;
  REG_FLOAT_IN(SetFloatValue);
  REG_INT_IN(SetIntValue);
  REG_LIST_IN(SetListValue);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // Four outlets, always, whatever the pattern says: unlike .if's out2 the
  // shape here does not depend on the parameter, so a patch can be wired
  // before the pattern is known.
  ADD_OUT_LIST; // 0 - the subject with every match substituted
  ADD_OUT_LIST; // 1 - the capture groups of one match
  ADD_OUT_LIST; // 2 - one matched substring
  ADD_OUT_LIST; // 3 - the message, unchanged, when nothing matched

  ADD_PARAM(pattern);
  ADD_PARAM(substitution);

  // The allocations the message path would otherwise need.
  result.reserve(kRegexpMaxOutput);
  groups.reserve(kRegexpMaxOutput);
  fragment.reserve(kRegexpMaxOutput);
  input.reserve(kExprValueTextMax);

  ADD_DESCRIPTION(
      "Regular-expression matching and substitution on symbols: pattern-matching and "
      "search-and-replace on the messages flowing through a patch, so an address, a name or a "
      "field can be picked apart without a round trip through the host application. The first "
      "creation argument is the pattern and everything after it is an optional substitution "
      "string, e.g. \".regexp ^/synth/(\\d+)/(\\w+)$ %2\". In a substitution, %1-%9 stand for the "
      "capture groups, %0 for the whole match and %% for a literal percent sign. The four outlets "
      "fire right to left, as in Max: a message the pattern does not match anywhere leaves the "
      "rightmost outlet unchanged and nothing else is sent, which is how the object works as a "
      "filter; a message that does match sends the matched substring and then that match's "
      "capture groups, once per match from left to right, and finally - only when a substitution "
      "is set - the whole subject with every match replaced. Ints and floats are matched as their "
      "text. The pattern language is a PCRE-flavoured core: literals, . for any character, "
      "character classes with ranges and negation, the \\d \\D \\w \\W \\s \\S shorthands, "
      "backslash escapes including \\xHH, the anchors ^ and $, the quantifiers * + ? and {n,m} "
      "(greedy, or lazy with a trailing ?), alternation with | and up to nine capturing groups "
      "plus (?: ) for a non-capturing one. Backreferences, lookaround, named groups and \\b are "
      "rejected when the pattern is compiled. Because the patcher splits parameters on spaces, a "
      "pattern cannot contain a literal space: write \\s for any whitespace or \\x20 for exactly "
      "one. The pattern is compiled once, when the parameter is set, into a fixed instruction "
      "array; matching runs a backtracking VM over that array with a step budget and writes into "
      "buffers reserved up front, so it allocates nothing, takes no lock and cannot run away - "
      "std::regex is deliberately not used because it allocates and throws at match time and its "
      "backtracking is unbounded. A malformed pattern is reported to the log when it is compiled "
      "and the object then passes every message out the no-match outlet.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "The subject to match. A symbol or list is matched as it stands; an int or a float is "
            "matched as its text.",
            "any symbol");
  OUTLET_DOC(0, "substitution",
             "The whole subject with every match replaced by the expanded substitution string. "
             "Sent once, after the per-match outlets, and only when a substitution parameter is "
             "set and the pattern matched at least once.",
             "symbol");
  OUTLET_DOC(1, "groups",
             "The capture groups of one match, as a space-separated list, sent once per match. "
             "Silent when the pattern declares no groups; a group that did not take part in the "
             "match contributes an empty token.",
             "symbol");
  OUTLET_DOC(2, "match", "One matched substring, sent once per match, left to right.", "symbol");
  OUTLET_DOC(3, "no match",
             "The message, unchanged, when the pattern matched nowhere in it - and the only "
             "outlet that fires in that case. Also the outlet an inert object (no pattern, or a "
             "pattern that did not compile) sends everything to.",
             "symbol");
  PARAM_DOC("pattern", "",
            "The regular expression, as one token. Write \\s or \\x20 rather than a literal "
            "space, which the parameter tokenizer would eat.",
            "any pattern");
  PARAM_DOC("substitution", "",
            "Optional replacement text; everything after the pattern, joined with single spaces. "
            "%1-%9 expand to the capture groups, %0 to the whole match, %% to a literal percent "
            "sign. Without it, outlet 0 never fires.",
            "any text");
}

PARM_CLEAR() {
  // Back to the bare object the constructor built. The ports are not touched
  // — unlike .if, .regexp's shape does not depend on its parameters.
  pattern.clear();
  substitution.clear();
  program.Clear();
  replacement.clear();
  error.clear();
  hasReplacement = false;
  valid = false;
}

PARM_PARSE() {
  // Parameters::Set pushed one token per space into `substitution`; join them
  // back with single spaces. Runs of spaces do not survive, but GetParams()
  // still returns the string the caller passed, so a DumpJSON round trip is
  // exact.
  replacement.clear();
  for (const std::string& token : substitution) {
    if (token.empty()) continue;
    if (!replacement.empty()) replacement += ' ';
    replacement += token;
  }
  hasReplacement = !replacement.empty();

  if (pattern.empty()) return; // no pattern yet: a bare, inert object

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the clear/parse callbacks
  // make ParamsNeedRebuild() true (issue #234).
  if (!program.Compile(pattern)) {
    error = program.Error();
    INTERNAL::LogImpl().emit(E_ERROR,
                             "patcher: .regexp cannot compile \"" + pattern + "\": " + error);
    // Compile() already left the program inert; an object that could not read
    // its pattern must pass messages through, not match them.
    return;
  }
  valid = true;
}

bool gRegexp::Append(std::string& destination, const char* data, int length) {
  if (length <= 0) return true;
  if (destination.size() + (std::size_t)length > (std::size_t)kRegexpMaxOutput) return false;
  destination.append(data, (std::size_t)length);
  return true;
}

bool gRegexp::AppendReplacement(const char* text, const RegexMatch& match) {
  const std::size_t size = replacement.size();
  for (std::size_t i = 0; i < size; i++) {
    const char c = replacement[i];
    if (c != '%' || i + 1 >= size) {
      if (!Append(result, &replacement[i], 1)) return false;
      continue;
    }

    const char next = replacement[i + 1];
    if (next >= '0' && next <= '9') {
      i++;
      const int group = next - '0';
      // A group the pattern does not declare, or one that did not take part
      // in this match, expands to nothing — the same answer as an empty
      // capture, and better than leaving "%3" in the output.
      if (group > program.GroupCount() || !match.Taken(group)) continue;
      if (!Append(result, text + match.begin[group], match.Length(group))) return false;
      continue;
    }

    // "%%" is a literal percent sign, and so is a "%" in front of anything
    // else — there is nothing useful to do with "%z" but print it.
    if (next == '%') i++;
    if (!Append(result, "%", 1)) return false;
  }
  return true;
}

void gRegexp::BuildGroups(const char* text, const RegexMatch& match) {
  groups.clear();
  const int count = program.GroupCount();
  for (int group = 1; group <= count; group++) {
    if (group > 1 && !Append(groups, " ", 1)) return;
    if (!match.Taken(group)) continue;
    if (!Append(groups, text + match.begin[group], match.Length(group))) return;
  }
}

void gRegexp::Process(const std::string& subject, YSE::THREAD thread) {
  const char* text = subject.c_str();
  const int length = (int)subject.size();

  // No pattern, a pattern that did not compile, or a subject longer than the
  // buffers were sized for: pass it on untouched. Sending the caller's own
  // string costs nothing — no copy, no allocation.
  if (!valid || length > kRegexpMaxSubject) {
    outputs[3].SendList(subject, thread);
    return;
  }

  // The whole RT budget for this message: one counter shared by every match
  // attempt of the scan, so the cost is bounded for the message and not just
  // per attempt.
  int budget = kRegexpStepBudget;
  bool matched = false;
  bool overflow = false;
  int pos = 0;
  result.clear();

  while (pos <= length) {
    RegexMatch match;
    if (!program.Search(text, length, pos, match, budget)) break;
    matched = true;

    const int begin = match.begin[0];
    const int end = match.end[0];

    // The substituted subject, built as the scan goes: everything skipped
    // since the previous match, then this match's replacement.
    if (hasReplacement && !overflow) {
      overflow = !Append(result, text + pos, begin - pos) || !AppendReplacement(text, match);
    }

    // Right to left, Max's convention: the matched substring, then its groups.
    fragment.assign(text + begin, (std::size_t)match.Length(0));
    outputs[2].SendList(fragment, thread);
    if (program.GroupCount() > 0) {
      BuildGroups(text, match);
      outputs[1].SendList(groups, thread);
    }

    if (end > begin) {
      pos = end;
      continue;
    }
    // An empty match — `a*` against "b" — would otherwise match at the same
    // place forever. Step over one character, carrying it into the result.
    if (hasReplacement && !overflow && begin < length) {
      overflow = !Append(result, text + begin, 1);
    }
    pos = begin + 1;
  }

  if (!matched) {
    outputs[3].SendList(subject, thread);
    return;
  }

  if (hasReplacement && !overflow) {
    // pos runs one past the end after a trailing empty match.
    if (pos < length) overflow = !Append(result, text + pos, length - pos);
    if (!overflow) outputs[0].SendList(result, thread);
  }
}

FLOAT_IN(SetFloatValue) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  input.assign(text, (std::size_t)length);
  Process(input, thread);
}

INT_IN(SetIntValue) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  input.assign(text, (std::size_t)length);
  Process(input, thread);
}

LIST_IN(SetListValue) {
  Process(value, thread);
}

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

  // The pattern is compiled once, on the control thread, into a fixed
  // instruction array; matching runs gRegexEngine's bounded backtracking VM
  // with a step budget, into buffers reserved above. So no message path
  // allocates, locks or blocks. gRegexp.h and gRegexEngine.h explain why this
  // is not std::regex.
  ADD_DESCRIPTION("Matches messages against a regular expression. Each match leaves with its "
                  "capture groups, an optional substitution rewrites every match, and a message "
                  "that does not match leaves the rightmost outlet unchanged.");
  ADD_CATEGORY(pCategory::STRING);
  INLET_DOC(0, "in",
            "The message to match. A list is matched as its text, an int or a float as the text "
            "that spells it.",
            "any symbol");
  OUTLET_DOC(0, "substitution",
             "The whole message with every match replaced by the substitution. Sent once, after "
             "the matches, and only when a substitution is set and something matched.",
             "symbol");
  OUTLET_DOC(1, "groups",
             "The capture groups of one match, as a list, sent once per match. Silent when the "
             "pattern has no groups. A group that took no part in the match is empty.",
             "symbol");
  OUTLET_DOC(2, "match", "One matched piece of text, sent once per match, left to right.",
             "symbol");
  OUTLET_DOC(3, "no match",
             "The message, unchanged, when the pattern matches nowhere in it. Also receives every "
             "message while there is no pattern, or the pattern could not be compiled.",
             "symbol");
  PARAM_DOC("pattern", "", "The regular expression, as one token. Write \\s or \\x20 for a space.",
            "any pattern");
  PARAM_DOC("substitution", "",
            "Optional replacement text: everything after the pattern, joined with single spaces. "
            "%1-%9 insert a capture group, %0 the whole match and %% a percent sign. Without it, "
            "outlet 0 never sends.",
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

#include "gIf.h"
#include "../pObjectList.hpp"
#include "../../implementations/logImplementation.h"

using namespace YSE::PATCHER;

#define className gIf

namespace {

  // Inlet labels follow the placeholders rather than the ports: the author of a
  // statement counts $1..$9, not 0..8. Same convention as .expr / .vexpr.
  const char* const kInletLabels[] = {"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8", "$9"};

  // "out1".."out9". Only "out2" means anything here, but the whole family is
  // recognised so a patch ported from Max gets an explanation rather than
  // "unknown function 'out3'".
  bool IsOutKeyword(const std::string& token) {
    return token.size() == 4 && token.compare(0, 3, "out") == 0 && token[3] >= '1' &&
           token[3] <= '9';
  }

} // namespace

CONSTRUCT() {
  // Room for all nine inlets and both outlets up front, so the emplace_back
  // calls in ParseParams can never reallocate. The patcher itself always
  // re-parses on an unpublished object (see
  // patcherImplementation::ReplaceObjectUnlocked), but a standalone object — a
  // unit test, an embedder driving pObject directly — can be wired first and
  // re-parsed after, and a reallocation would leave the peers holding
  // inlet/outlet pointers into freed storage.
  inputs.reserve(kExprMaxVars);
  outputs.reserve(2);

  // The left inlet always exists — a statement whose condition names no
  // placeholder at all can still be banged. Inlets 1..8 are created in
  // ParseParams, once the statement says how many of them the patch needs.
  ADD_IN_0;
  REG_FLOAT_IN(SetFloat);
  REG_INT_IN(SetInt);
  REG_BANG_IN(SetBang);
  REG_LIST_IN(SetList);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // ANY rather than a concrete type: a message can be a bang, an int, a float
  // or a list, and which one it is depends on the statement. The second outlet
  // is created in ParseParams, and only when a message asks for it with out2.
  ADD_OUT_ANY;

  ADD_PARAM(statement);

  // The one allocation the output path would otherwise need. Every item is at
  // most kExprValueTextMax - 1 characters and they are joined by single spaces,
  // so this is an upper bound on any list the object can send.
  result.reserve((std::size_t)kIfMaxMessageItems * (std::size_t)kExprValueTextMax);

  ADD_DESCRIPTION(
      "Conditional message dispatch: one object in place of a comparison box plus a gate plus a "
      "select. The creation argument reads as a sentence - \"$i1 > 64 then bang else out2 $i1\" - "
      "and means what it says: evaluate the condition, send the then message when it is non-zero, "
      "send the else message when it is not. With no else, a false condition sends nothing at all, "
      "which is how a stream gets filtered rather than mapped. The condition is everything before "
      "the word then and is an .expr expression: the same language, the same $i1-$i9 and $f1-$f9 "
      "placeholders ($i reads an inlet as an int, $f as a float), the same C operators, functions "
      "and type rules - read .expr for the details. A message is a space-separated list of items, "
      "each of which is itself an expression, so a literal (1, 0.5), a placeholder ($i1) or any "
      "space-free expression ($i1*2) all work; one item is sent as an int or a float following its "
      "type and two or more are sent as a list, while the word bang on its own sends a bang. The "
      "keyword out2 in front of a message routes it to a second, right outlet, which the object "
      "grows only when a message asks for it. The number of inlets is one past the highest "
      "placeholder used anywhere in the statement, capped at nine; inlet 0 is hot (it stores its "
      "value and evaluates) while the other inlets only store, a bang on inlet 0 re-evaluates from "
      "the stored values and a list fills the inlets left to right. Max's send keyword and its $s "
      "symbol placeholders are not supported and are rejected when the statement is compiled - "
      "wire the outlet to a .s object instead. The statement is compiled once, when the parameter "
      "is set; evaluation only walks the compiled form and writes into a buffer reserved up front, "
      "so it allocates nothing and takes no lock. A malformed statement is reported to the log at "
      "parse time and the object then sends nothing.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, kInletLabels[0],
            "Value for the $i1 / $f1 placeholders — stores and fires the evaluation. Also accepts "
            "a bang (re-evaluate with the stored values) and a list (fill the inlets left to "
            "right, then evaluate).",
            "any float");
  OUTLET_DOC(0, "out",
             "The then or else message, unless it was prefixed with out2. A single-item message "
             "leaves as an int or a float following its type, a longer one as a list, and the "
             "word bang as a bang. Nothing is sent when the branch that was taken has no message.",
             "any");
  PARAM_DOC("statement", "",
            "The conditional statement, e.g. \"$i1 > 64 then bang else out2 $i1\": a condition, "
            "the word then, a message, and optionally the word else and a second message.",
            "any statement");
}

PARM_CLEAR() {
  // Back to the bare object the constructor built: one inlet, one outlet, no
  // programs. The stored inlet values go too — a value received for an inlet
  // that the new statement re-uses for something else would otherwise leak
  // across, and Max treats a placeholder that has never received a value as 0.
  while (inputs.size() > 1) {
    inputs.pop_back();
  }
  while (outputs.size() > 1) {
    outputs.pop_back();
  }
  for (int i = 0; i < kExprMaxVars; i++) {
    vars[i] = 0.f;
  }
  statement.clear();
  condition.Clear();
  thenMsg.Reset();
  elseMsg.Reset();
  error.clear();
  usedInlets = 0;
  valid = false;
}

bool gIf::Fail(const std::string& message) {
  if (error.empty()) error = message;
  return false;
}

bool gIf::CompileMessage(const std::vector<std::string>& tokens, std::size_t from, std::size_t to,
                         const char* which, Message& out) {
  const std::string label = std::string("the '") + which + "' message";
  if (from >= to) return Fail(label + " is missing");

  std::size_t i = from;

  // Max: "Messages can be sent to remote receive objects by preceding the
  // message expression with send". That would mean a name lookup on the audio
  // thread; wiring the outlet to a .s object costs nothing and is visible in
  // the patch, so say so rather than failing on "unknown function 'send'".
  if (tokens[i] == "send") {
    return Fail(label + " uses 'send', which .if does not support — wire the outlet to a .s "
                        "object instead");
  }

  if (IsOutKeyword(tokens[i])) {
    if (tokens[i] != "out2") {
      return Fail("'" + tokens[i] + "' in " + label +
                  ": only 'out2' is supported, .if has at most two outlets");
    }
    out.toRight = true;
    i++;
    if (i >= to) return Fail(label + " is empty after 'out2'");
  }

  // A lone "bang" is the one message that is a word rather than a number.
  if (to - i == 1 && tokens[i] == "bang") {
    out.isBang = true;
    out.present = true;
    return true;
  }

  for (; i < to; i++) {
    const std::string& token = tokens[i];
    if (token == "bang") {
      return Fail("'bang' can only be " + label + " on its own, not one item of a list");
    }
    // The messages below are built with appends rather than the obvious `a + b
    // + c`: every one of them is a one-shot return out of this loop, but a
    // chain of operator+ inside a loop is a clang-tidy finding regardless
    // (performance-inefficient-string-concatenation), and appending is no
    // harder to read.
    if (token == "then" || token == "else") {
      std::string message = "'";
      message += token;
      message += "' cannot appear inside ";
      message += label;
      return Fail(message);
    }
    if (IsOutKeyword(token)) {
      std::string message = "'";
      message += token;
      message += "' must be the first word of ";
      message += label;
      return Fail(message);
    }
    if (out.count >= kIfMaxMessageItems) {
      return Fail(label + " holds more than " + std::to_string(kIfMaxMessageItems) + " items");
    }
    // The reuse that makes this object small: every item is compiled by the
    // same evaluator the condition uses, so a literal, a placeholder and a
    // space-free expression all come out of one code path.
    if (!out.items[out.count].Compile(token)) {
      std::string message = label;
      message += " item \"";
      message += token;
      message += "\" does not compile: ";
      message += out.items[out.count].Error();
      return Fail(message);
    }
    out.count++;
  }

  out.present = true;
  return true;
}

bool gIf::CompileStatement(const std::vector<std::string>& tokens) {
  std::size_t thenAt = tokens.size();
  for (std::size_t i = 0; i < tokens.size(); i++) {
    if (tokens[i] == "then") {
      thenAt = i;
      break;
    }
  }
  if (thenAt == tokens.size()) {
    return Fail(
        "the statement needs a 'then' keyword: <condition> then <message> [else <message>]");
  }
  if (thenAt == 0) return Fail("the condition before 'then' is missing");

  // Parameters::Set split the argument string on spaces; glue the condition
  // back together before compiling it. Whitespace is not significant inside an
  // expression, and GetParams() still returns the string the caller passed, so
  // a DumpJSON round trip is exact.
  std::string source;
  for (std::size_t i = 0; i < thenAt; i++) {
    if (i != 0) source += ' ';
    source += tokens[i];
  }
  if (!condition.Compile(source)) {
    return Fail("the condition \"" + source + "\" does not compile: " + condition.Error());
  }

  // The first 'else' after 'then' splits the two messages. A second one lands
  // inside the else message and is rejected there, by name.
  std::size_t elseAt = tokens.size();
  for (std::size_t i = thenAt + 1; i < tokens.size(); i++) {
    if (tokens[i] == "else") {
      elseAt = i;
      break;
    }
  }

  if (!CompileMessage(tokens, thenAt + 1, elseAt, "then", thenMsg)) return false;
  if (elseAt != tokens.size()) {
    if (!CompileMessage(tokens, elseAt + 1, tokens.size(), "else", elseMsg)) return false;
  }
  return true;
}

PARM_PARSE() {
  // Parameters::Set pushes one token per space, so consecutive spaces leave
  // empty entries behind; drop them rather than trying to compile "".
  std::vector<std::string> tokens;
  tokens.reserve(statement.size());
  for (const std::string& token : statement) {
    if (!token.empty()) tokens.push_back(token);
  }
  if (tokens.empty()) return; // no statement yet: a bare, inert object

  // The one compile. Control thread, at construction or on SetParams; a live
  // SetParams cannot reach here in place because the clear/parse callbacks
  // make ParamsNeedRebuild() true (issue #234).
  if (!CompileStatement(tokens)) {
    INTERNAL::LogImpl().emit(E_ERROR,
                             "patcher: .if cannot compile \"" + parms.Get() + "\": " + error);
    // Inert rather than half-built: a statement that did not compile must send
    // nothing, not "the else branch".
    condition.Clear();
    thenMsg.Reset();
    elseMsg.Reset();
    return;
  }
  valid = true;

  // How many inlets the statement needs: one past the highest placeholder it
  // mentions anywhere. ExprProgram::InletCount() is never below 1, so a
  // statement with no placeholder at all still gets its left inlet.
  int wanted = condition.InletCount();
  const Message* messages[2] = {&thenMsg, &elseMsg};
  for (const Message* message : messages) {
    for (int i = 0; i < message->count; i++) {
      const int needed = message->items[i].InletCount();
      if (needed > wanted) wanted = needed;
    }
  }

  // ...and which of them are actually read, for the per-inlet documentation.
  for (int i = 0; i < kExprMaxVars; i++) {
    bool used = condition.UsesInlet(i);
    for (const Message* message : messages) {
      for (int k = 0; k < message->count && !used; k++) {
        used = message->items[k].UsesInlet(i);
      }
    }
    if (used) usedInlets |= (1u << i);
  }

  while ((int)inputs.size() < wanted) {
    const std::size_t index = inputs.size();
    inputs.emplace_back(this, false, (int)index);
    REG_FLOAT_IN(SetFloat);
    REG_INT_IN(SetInt);
    inputs.back().SetDoc(kInletLabels[index],
                         UsesInlet((int)index)
                             ? "Value for the $i / $f placeholders with this index — stored until "
                               "the next evaluation."
                             : "Unused by the current statement; it exists so the inlets to its "
                               "right keep their numbering.",
                         "any float");
  }

  // Max: "The keyword out2 in a message expression creates a second, right
  // outlet." No message asking for it means no outlet, so a plain .if looks
  // like every other one-outlet object.
  if ((thenMsg.toRight || elseMsg.toRight) && outputs.size() < 2) {
    ADD_OUT_ANY;
    outputs.back().SetDoc("out2",
                          "The then or else message that was prefixed with out2. Same rules as "
                          "the left outlet: an int, a float, a list or a bang, following the "
                          "message.",
                          "any");
  }
}

void gIf::Store(float value, int inlet) {
  if (inlet >= 0 && inlet < kExprMaxVars) {
    vars[inlet] = value;
  }
}

FLOAT_IN(SetFloat) {
  Store(value, inlet);
}

INT_IN(SetInt) {
  Store((float)value, inlet);
}

BANG_IN(SetBang) {
  // Nothing to store; inlet 0 is active, so the evaluation follows.
}

LIST_IN(SetList) {
  // As in .expr: "the items of a list received in the left inlet are treated as
  // if each had come in a different inlet". Items past the ninth are dropped,
  // and an inlet the list does not reach keeps the value it already had.
  //
  // Parsed with strtof over the characters rather than by splitting into
  // strings: a list can arrive on the audio thread, where an allocation is not
  // allowed.
  float values[kExprMaxVars];
  const int count = ExprParseFloatList(value.c_str(), values, kExprMaxVars);
  for (int i = 0; i < count; i++) {
    vars[i] = values[i];
  }
}

void gIf::Emit(const Message& message, YSE::THREAD thread) {
  const int port = (message.toRight && outputs.size() > 1) ? 1 : 0;

  if (message.isBang) {
    outputs[port].SendBang(thread);
    return;
  }

  // One item leaves as a bare int or float rather than as a one-item list,
  // which is what Max does and what the downstream objects expect.
  if (message.count == 1) {
    const ExprValue value = message.items[0].Evaluate(vars);
    if (value.isInt) {
      outputs[port].SendInt(value.AsInt(), thread);
    } else {
      outputs[port].SendFloat(value.AsFloat(), thread);
    }
    return;
  }

  result.clear();
  for (int i = 0; i < message.count; i++) {
    if (i != 0) result += ' ';
    char text[kExprValueTextMax];
    const int length = ExprFormatValue(message.items[i].Evaluate(vars), text, kExprValueTextMax);
    result.append(text, (std::size_t)length);
  }
  outputs[port].SendList(result, thread);
}

CALC() {
  // The entire RT budget: a walk of the pre-compiled condition over a stack in
  // this frame, then at most one message's worth of the same. No parsing, no
  // allocation, no lock, no I/O.
  if (!valid) return;

  const ExprValue test = condition.Evaluate(vars);
  const bool taken = test.isInt ? (test.i != 0) : (test.f != 0.f);

  if (taken) {
    Emit(thenMsg, thread);
  } else if (elseMsg.present) {
    Emit(elseMsg, thread);
  }
}

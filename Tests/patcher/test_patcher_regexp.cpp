// Tests for the regular-expression object (issue #452): .regexp.
//
// Two layers get covered here, because the object is two things:
//
//   - **the engine** (patcher/genericObjects/gRegexEngine.h). std::regex was
//     ruled out for this object — it allocates and throws at match time and
//     its backtracking is unbounded — so the pattern language is implemented
//     here and has to be pinned here: every construct it supports, every
//     construct it deliberately refuses, and the bounds that make it RT-safe.
//     The malformed-pattern cases are not decoration: "fails loudly at compile
//     time, then passes messages through" is the whole safety story, and the
//     step budget is what keeps a catastrophic pattern like (a+)+b from
//     hanging the thread that fed it.
//   - **the object**: which outlet fires when, the per-match scan, the
//     substitution expansion, the int/float-as-text inputs, the params round
//     trip and the doc metadata every patcher object owes.
//
// No audio device required.

#include <doctest/doctest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include "patcher/patcher.hpp"
#include "patcher/pHandle.hpp"
#include "patcher/pObjectList.hpp"
#include "patcher/pRegistry.h"
#include "patcher/genericObjects/gRegexp.h"
#include "patcher/genericObjects/gRegexEngine.h"

using YSE::PATCHER::gRegexp;
using YSE::PATCHER::kRegexpMaxSubject;
using YSE::PATCHER::kRegexpStepBudget;
using YSE::PATCHER::RegexMatch;
using YSE::PATCHER::RegexProgram;

namespace {

  // MultiSink keeps only the last message; .regexp sends once per match, so
  // the interesting assertion is usually "what did this outlet send, in
  // order".
  struct ListLog : YSE::PATCHER::pObject {
    std::vector<std::string> values;

    ListLog() : pObject(false) {
      inputs.emplace_back(this, true, 0);
      inputs.back().RegisterList(
          [this](const std::string& v, int, YSE::THREAD) { values.push_back(v); });
    }
    const char* Type() const override {
      return "list_log";
    }
    void Calculate(YSE::THREAD) override {}
    void SetMessage(const std::string&, float) override {}

    void reset() {
      values.clear();
    }
    bool silent() const {
      return values.empty();
    }
    std::size_t count() const {
      return values.size();
    }
    const std::string& only() const {
      return values.front();
    }
  };

  // A .regexp with a log on each of its four outlets. The parameters are set
  // before the outlets are wired, mirroring the patcher's own order.
  struct RegexpRig {
    gRegexp op;
    ListLog substitution; // outlet 0
    ListLog groups; // outlet 1
    ListLog match; // outlet 2
    ListLog nomatch; // outlet 3

    explicit RegexpRig(const std::string& params) {
      op.SetParams(params);
      ListLog* sinks[4] = {&substitution, &groups, &match, &nomatch};
      for (int i = 0; i < 4; i++) {
        op.ConnectOutlet(sinks[i]->GetInlet(0), i);
        sinks[i]->ConnectInlet(op.GetOutlet(i), 0);
      }
    }

    void Reset() {
      substitution.reset();
      groups.reset();
      match.reset();
      nomatch.reset();
    }

    void Feed(const std::string& subject) {
      Reset();
      op.GetInlet(0)->SetList(subject, YSE::T_GUI);
    }
    void FeedInt(int value) {
      Reset();
      op.GetInlet(0)->SetInt(value, YSE::T_GUI);
    }
    void FeedFloat(float value) {
      Reset();
      op.GetInlet(0)->SetFloat(value, YSE::T_GUI);
    }
  };

  // A pattern that must be rejected: the object reports why, stays inert, and
  // then passes every message straight out the no-match outlet rather than
  // pretending to match. Returns the error so a test can assert on what it
  // says as well as that it happened.
  std::string CompileError(const std::string& pattern) {
    RegexpRig rig(pattern);
    CHECK_FALSE(rig.op.Valid());
    CHECK_FALSE(rig.op.CompileError().empty());

    rig.Feed("anything at all");
    CHECK(rig.nomatch.count() == 1);
    CHECK(rig.match.silent());
    CHECK(rig.groups.silent());
    CHECK(rig.substitution.silent());
    return rig.op.CompileError();
  }

  // Engine-level convenience: the text of the leftmost match of `pattern` in
  // `subject`, or "<none>".
  std::string FirstMatch(const std::string& pattern, const std::string& subject) {
    RegexProgram program;
    REQUIRE(program.Compile(pattern));
    RegexMatch m;
    int budget = kRegexpStepBudget;
    if (!program.Search(subject.c_str(), (int)subject.size(), 0, m, budget)) return "<none>";
    return subject.substr((std::size_t)m.begin[0], (std::size_t)m.Length(0));
  }

} // namespace

TEST_SUITE("patcher") {

  // ═══ registration ═════════════════════════════════════════════════════════

  TEST_CASE("regexp: the object is creatable through the registry (#452)") {
    YSE::patcher p;
    p.create(2);
    YSE::pHandle* h = p.CreateObject(YSE::OBJ::G_REGEXP);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".regexp"));
    // The shape does not depend on the pattern: one inlet, four outlets,
    // always, so a patch can be wired before the pattern is known.
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 4);
  }

  TEST_CASE("regexp: the object is listed by pRegistry::AllNames (#452)") {
    const auto names = YSE::PATCHER::Register().AllNames();
    CHECK(std::find(names.begin(), names.end(), std::string(".regexp")) != names.end());
  }

  TEST_CASE("regexp: names both of its parameters (#452)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_REGEXP));
    REQUIRE(obj != nullptr);
    const auto& docs = obj->GetParamDocs();
    REQUIRE(docs.size() == 2);
    CHECK(docs[0].name == "pattern");
    CHECK(docs[1].name == "substitution");
  }

  TEST_CASE("regexp: documents itself (#452)") {
    std::unique_ptr<YSE::PATCHER::pObject> obj(YSE::PATCHER::Register().Get(YSE::OBJ::G_REGEXP));
    REQUIRE(obj != nullptr);
    CHECK_FALSE(obj->GetDescription().empty());
    CHECK(obj->GetCategory() == YSE::PATCHER::pCategory::GENERIC);
    REQUIRE(obj->NumInputs() == 1);
    REQUIRE(obj->NumOutputs() == 4);
    for (int i = 0; i < obj->NumOutputs(); i++) {
      CAPTURE(i);
      CHECK_FALSE(obj->GetOutlet(i)->GetDocLabel().empty());
      CHECK_FALSE(obj->GetOutlet(i)->GetDocDescription().empty());
    }
  }

  // ═══ the object with no usable pattern ════════════════════════════════════

  TEST_CASE("regexp: a bare object passes everything to the no-match outlet (#452)") {
    RegexpRig rig("");
    CHECK_FALSE(rig.op.Valid());

    rig.Feed("/synth/1/gain");
    CHECK(rig.nomatch.count() == 1);
    CHECK(rig.nomatch.only() == "/synth/1/gain");
    CHECK(rig.match.silent());
    CHECK(rig.groups.silent());
    CHECK(rig.substitution.silent());
  }

  // ═══ matching ═════════════════════════════════════════════════════════════

  TEST_CASE("regexp: a literal pattern reports the substring it matched (#452)") {
    RegexpRig rig("gain");
    REQUIRE(rig.op.Valid());

    rig.Feed("/synth/1/gain");
    REQUIRE(rig.match.count() == 1);
    CHECK(rig.match.only() == "gain");
    CHECK(rig.nomatch.silent());
    CHECK(rig.groups.silent()); // no capture groups in the pattern
    CHECK(rig.substitution.silent()); // no substitution parameter
  }

  TEST_CASE("regexp: a subject that does not match leaves the fourth outlet (#452)") {
    RegexpRig rig("gain");
    rig.Feed("/synth/1/pan");
    REQUIRE(rig.nomatch.count() == 1);
    CHECK(rig.nomatch.only() == "/synth/1/pan");
    CHECK(rig.match.silent());
    CHECK(rig.groups.silent());
    CHECK(rig.substitution.silent());
  }

  TEST_CASE("regexp: capture groups leave the second outlet as a list (#452)") {
    RegexpRig rig(R"(^/synth/(\d+)/(\w+)$)");
    REQUIRE(rig.op.Valid());
    CHECK(rig.op.GroupCount() == 2);

    rig.Feed("/synth/12/gain");
    REQUIRE(rig.match.count() == 1);
    CHECK(rig.match.only() == "/synth/12/gain");
    REQUIRE(rig.groups.count() == 1);
    CHECK(rig.groups.only() == "12 gain");
    CHECK(rig.nomatch.silent());
  }

  TEST_CASE("regexp: a group that did not take part contributes an empty token (#452)") {
    RegexpRig rig("a(x)?b(y)?");
    REQUIRE(rig.op.Valid());
    REQUIRE(rig.op.GroupCount() == 2);

    rig.Feed("ab");
    REQUIRE(rig.groups.count() == 1);
    CHECK(rig.groups.only() == " ");
  }

  TEST_CASE("regexp: every non-overlapping match is reported, left to right (#452)") {
    RegexpRig rig(R"((\d+))");
    rig.Feed("a1 bb22 c333");
    REQUIRE(rig.match.count() == 3);
    CHECK(rig.match.values[0] == "1");
    CHECK(rig.match.values[1] == "22");
    CHECK(rig.match.values[2] == "333");
    REQUIRE(rig.groups.count() == 3);
    CHECK(rig.groups.values[2] == "333");
    CHECK(rig.nomatch.silent());
  }

  TEST_CASE("regexp: anchors pin the match to the whole subject (#452)") {
    RegexpRig rig("^abc$");
    rig.Feed("abc");
    CHECK(rig.match.count() == 1);

    rig.Feed("xabc");
    CHECK(rig.match.silent());
    CHECK(rig.nomatch.count() == 1);

    rig.Feed("abcx");
    CHECK(rig.match.silent());
    CHECK(rig.nomatch.count() == 1);
  }

  // ═══ substitution ═════════════════════════════════════════════════════════

  TEST_CASE("regexp: a substitution rewrites the whole subject on outlet 0 (#452)") {
    RegexpRig rig(R"(^/synth/(\d+)/(\w+)$ %2)");
    REQUIRE(rig.op.Valid());
    CHECK(rig.op.HasSubstitution());
    CHECK(rig.op.Substitution() == "%2");

    rig.Feed("/synth/12/gain");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "gain");
  }

  TEST_CASE("regexp: the substitution keeps the text around every match (#452)") {
    RegexpRig rig(R"((\d+) <%0>)");
    rig.Feed("a1 bb22 c");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "a<1> bb<22> c");
  }

  TEST_CASE("regexp: a multi-word substitution is joined with single spaces (#452)") {
    RegexpRig rig(R"(^(\w+)-(\w+)$ %2 and %1)");
    CHECK(rig.op.Substitution() == "%2 and %1");
    rig.Feed("left-right");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "right and left");
  }

  TEST_CASE("regexp: %% is a literal percent sign, %n of an absent group is nothing (#452)") {
    RegexpRig rig("(a)|(b) %%%1%2");
    REQUIRE(rig.op.Valid());
    rig.Feed("a");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "%a");
    rig.Feed("b");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "%b");
  }

  TEST_CASE("regexp: without a substitution parameter outlet 0 never fires (#452)") {
    RegexpRig rig("a");
    CHECK_FALSE(rig.op.HasSubstitution());
    rig.Feed("banana");
    CHECK(rig.match.count() == 3);
    CHECK(rig.substitution.silent());
  }

  TEST_CASE("regexp: a nowhere-matching subject sends no substitution either (#452)") {
    RegexpRig rig("z Z");
    rig.Feed("abc");
    CHECK(rig.substitution.silent());
    REQUIRE(rig.nomatch.count() == 1);
    CHECK(rig.nomatch.only() == "abc");
  }

  TEST_CASE("regexp: an empty match steps forward instead of looping (#452)") {
    // Perl's s/x*/-/g answer, and the case that would spin forever if the
    // scan did not step over a zero-length match.
    RegexpRig rig("x* -");
    REQUIRE(rig.op.Valid());
    rig.Feed("abc");
    REQUIRE(rig.substitution.count() == 1);
    CHECK(rig.substitution.only() == "-a-b-c-");
    CHECK(rig.match.count() == 4);
  }

  TEST_CASE("regexp: a substitution that would outgrow the output buffer is dropped (#452)") {
    // The buffers are reserved at construction and never grown, so the object
    // has to refuse rather than reallocate on the message path. The per-match
    // outlets still fire — only the oversized result is withheld.
    RegexpRig rig("a %0%0%0%0%0%0%0%0%0%0");
    REQUIRE(rig.op.Valid());
    const std::string subject(kRegexpMaxSubject, 'a');
    rig.Feed(subject);
    CHECK(rig.match.count() == (std::size_t)kRegexpMaxSubject);
    CHECK(rig.substitution.silent());
  }

  // ═══ numeric input ════════════════════════════════════════════════════════

  TEST_CASE("regexp: ints and floats are matched as their text (#452)") {
    RegexpRig rig(R"(^(\d+)\.(\d+)$)");
    REQUIRE(rig.op.Valid());

    rig.FeedFloat(12.5f);
    REQUIRE(rig.match.count() == 1);
    CHECK(rig.match.only() == "12.5");
    REQUIRE(rig.groups.count() == 1);
    CHECK(rig.groups.only() == "12 5");

    // An int has no decimal point, so the same pattern does not match it.
    rig.FeedInt(12);
    CHECK(rig.match.silent());
    REQUIRE(rig.nomatch.count() == 1);
    CHECK(rig.nomatch.only() == "12");
  }

  // ═══ bounds ═══════════════════════════════════════════════════════════════

  TEST_CASE("regexp: a subject longer than the cap is passed through untouched (#452)") {
    RegexpRig rig("a");
    const std::string subject(kRegexpMaxSubject + 1, 'a');
    rig.Feed(subject);
    REQUIRE(rig.nomatch.count() == 1);
    CHECK(rig.nomatch.only() == subject);
    CHECK(rig.match.silent());
  }

  TEST_CASE("regexp: a catastrophic pattern terminates instead of hanging (#452)") {
    // (a+)+b against a run of a's is the textbook exponential backtracking
    // case — the one std::regex has no answer for. Here the step budget ends
    // the scan, the object answers "no match", and the next message is
    // unaffected because the budget is per message.
    RegexpRig rig("(a+)+b");
    REQUIRE(rig.op.Valid());

    const std::string bomb(48, 'a');
    rig.Feed(bomb);
    CHECK(rig.match.silent());
    CHECK(rig.nomatch.count() == 1);

    // The budget did not leak: an ordinary subject still matches afterwards.
    rig.Feed("aaab");
    REQUIRE(rig.match.count() == 1);
    CHECK(rig.match.only() == "aaab");
  }

  TEST_CASE("regexp: the step budget is spent per message, not per object (#452)") {
    RegexpRig rig(R"((\d))");
    for (int i = 0; i < 8; i++) {
      rig.Feed("1234567890");
      CHECK(rig.match.count() == 10);
    }
  }

  // ═══ the pattern language ═════════════════════════════════════════════════

  TEST_CASE("regexp: character classes, ranges and negation (#452)") {
    CHECK(FirstMatch("[abc]+", "zzabcazz") == "abca");
    CHECK(FirstMatch("[a-f0-9]+", "zz1fz") == "1f");
    CHECK(FirstMatch("[^0-9]+", "12ab34") == "ab");
    CHECK(FirstMatch(R"([\d]+)", "ab99cd") == "99");
    CHECK(FirstMatch(R"([\D]+)", "99abc99") == "abc");
    // A '-' at the end of a class is a literal.
    CHECK(FirstMatch("[a-]+", "xa-ax") == "a-a");
  }

  TEST_CASE("regexp: the shorthand escapes (#452)") {
    CHECK(FirstMatch(R"(\d+)", "ab123cd") == "123");
    CHECK(FirstMatch(R"(\w+)", " ab_1 ") == "ab_1");
    CHECK(FirstMatch(R"(\s+)", "ab  cd") == "  ");
    CHECK(FirstMatch(R"(\S+)", "  ab  ") == "ab");
    CHECK(FirstMatch(R"(\W+)", "ab..cd") == "..");
  }

  TEST_CASE("regexp: greedy and lazy quantifiers differ (#452)") {
    CHECK(FirstMatch("<.+>", "<a><b>") == "<a><b>");
    CHECK(FirstMatch("<.+?>", "<a><b>") == "<a>");
    CHECK(FirstMatch("a*", "aaab") == "aaa");
    CHECK(FirstMatch("a*?", "aaab").empty()); // lazy: the empty match wins
  }

  TEST_CASE("regexp: counted repetition (#452)") {
    CHECK(FirstMatch("a{3}", "aaaaa") == "aaa");
    CHECK(FirstMatch("a{2,}", "baaaa") == "aaaa");
    CHECK(FirstMatch("a{2,3}", "aaaaa") == "aaa");
    CHECK(FirstMatch("a{2,3}", "xaax") == "aa");
    CHECK(FirstMatch("a{2,3}", "xax") == "<none>");
  }

  TEST_CASE("regexp: alternation and non-capturing groups (#452)") {
    CHECK(FirstMatch("cat|dog|bird", "a dog here") == "dog");
    CHECK(FirstMatch("(?:ab)+", "xababy") == "abab");

    RegexProgram program;
    REQUIRE(program.Compile("(?:ab)(cd)"));
    CHECK(program.GroupCount() == 1); // the (?: ) group does not count
  }

  TEST_CASE("regexp: escapes make the metacharacters literal (#452)") {
    CHECK(FirstMatch(R"(\.)", "ab.cd") == ".");
    CHECK(FirstMatch(R"(a\+b)", "xa+bx") == "a+b");
    CHECK(FirstMatch(R"(\(\))", "x()x") == "()");
    CHECK(FirstMatch(R"(\x41+)", "zzAAzz") == "AA");
    // \x20 is how a pattern names a space the parameter tokenizer would eat.
    CHECK(FirstMatch(R"(a\x20b)", "xa bx") == "a b");
  }

  TEST_CASE("regexp: group offsets are reported per group (#452)") {
    RegexProgram program;
    REQUIRE(program.Compile(R"((\w+)@(\w+))"));
    const std::string subject = "mail: name@host !";
    RegexMatch m;
    int budget = kRegexpStepBudget;
    REQUIRE(program.Search(subject.c_str(), (int)subject.size(), 0, m, budget));
    CHECK(subject.substr((std::size_t)m.begin[0], (std::size_t)m.Length(0)) == "name@host");
    CHECK(subject.substr((std::size_t)m.begin[1], (std::size_t)m.Length(1)) == "name");
    CHECK(subject.substr((std::size_t)m.begin[2], (std::size_t)m.Length(2)) == "host");
    CHECK_FALSE(m.Taken(3));
  }

  TEST_CASE("regexp: Search honours the start offset and the budget (#452)") {
    RegexProgram program;
    REQUIRE(program.Compile("a"));
    const std::string subject = "xaxa";
    RegexMatch m;

    int budget = kRegexpStepBudget;
    REQUIRE(program.Search(subject.c_str(), 4, 2, m, budget));
    CHECK(m.begin[0] == 3);

    // A budget of zero answers "no match" rather than running.
    int exhausted = 0;
    CHECK_FALSE(program.Search(subject.c_str(), 4, 0, m, exhausted));
  }

  // ═══ malformed patterns ═══════════════════════════════════════════════════

  TEST_CASE("regexp: an unclosed group is rejected (#452)") {
    CHECK(CompileError("(ab").find("unmatched '('") != std::string::npos);
  }

  TEST_CASE("regexp: a stray closing paren is rejected (#452)") {
    CHECK(CompileError("ab)").find("unmatched ')'") != std::string::npos);
  }

  TEST_CASE("regexp: an unterminated character class is rejected (#452)") {
    CHECK(CompileError("[abc").find("unterminated character class") != std::string::npos);
  }

  TEST_CASE("regexp: an empty character class is rejected (#452)") {
    CHECK(CompileError("a[]b").find("empty character class") != std::string::npos);
  }

  TEST_CASE("regexp: a reversed range in a character class is rejected (#452)") {
    CHECK(CompileError("[z-a]").find("counts down") != std::string::npos);
  }

  TEST_CASE("regexp: a quantifier with nothing to repeat is rejected (#452)") {
    CHECK(CompileError("*ab").find("nothing to repeat") != std::string::npos);
    CHECK(CompileError("a**").find("nothing to repeat") != std::string::npos);
    CHECK(CompileError("^*").find("nothing to repeat") != std::string::npos);
  }

  TEST_CASE("regexp: a malformed repeat count is rejected (#452)") {
    CHECK(CompileError("a{2,1}").find("counts down") != std::string::npos);
    CHECK(CompileError("a{2").find("unterminated '{'") != std::string::npos);
    CHECK(CompileError("a{999}").find("too large") != std::string::npos);
  }

  TEST_CASE("regexp: a trailing backslash is rejected (#452)") {
    CHECK(CompileError(R"(ab\)").find("lone backslash") != std::string::npos);
  }

  TEST_CASE("regexp: an unknown escape is rejected (#452)") {
    CHECK(CompileError(R"(a\qb)").find("unknown escape") != std::string::npos);
    CHECK(CompileError(R"(a\x2)").find("two hexadecimal digits") != std::string::npos);
  }

  TEST_CASE("regexp: the unsupported PCRE constructs are named, not ignored (#452)") {
    CHECK(CompileError(R"(a\1)").find("backreferences") != std::string::npos);
    CHECK(CompileError(R"(\bword)").find("word boundaries") != std::string::npos);
    CHECK(CompileError("(?=ab)").find("(?...)") != std::string::npos);
    CHECK(CompileError("a*+").find("possessive") != std::string::npos);
  }

  TEST_CASE("regexp: an unbounded repeat of a nullable body is rejected (#452)") {
    // (a*)* is an infinite loop in a backtracking VM. Refusing it when the
    // pattern is compiled beats letting the step budget quietly run out on
    // every message.
    const std::string error = CompileError("(a*)*");
    CHECK(error.find("empty string") != std::string::npos);
  }

  TEST_CASE("regexp: more than nine capturing groups is rejected (#452)") {
    CHECK(CompileError("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)").find("nine capturing groups") !=
          std::string::npos);
    // ...and exactly nine is fine.
    RegexProgram program;
    CHECK(program.Compile("(a)(b)(c)(d)(e)(f)(g)(h)(i)"));
    CHECK(program.GroupCount() == 9);
  }

  TEST_CASE("regexp: an over-long pattern is rejected (#452)") {
    const std::string huge(YSE::PATCHER::kRegexMaxPattern + 1, 'a');
    RegexProgram program;
    CHECK_FALSE(program.Compile(huge));
    CHECK(program.Error().find("longer than") != std::string::npos);
    CHECK(program.Size() == 0);
  }

  TEST_CASE("regexp: a compile failure leaves the program unusable, not half-built (#452)") {
    RegexProgram program;
    REQUIRE(program.Compile("abc"));
    CHECK(program.Valid());

    CHECK_FALSE(program.Compile("(abc"));
    CHECK_FALSE(program.Valid());
    CHECK(program.GroupCount() == 0);

    RegexMatch m;
    int budget = kRegexpStepBudget;
    CHECK_FALSE(program.Search("abc", 3, 0, m, budget));
  }

  // ═══ parameters ═══════════════════════════════════════════════════════════

  TEST_CASE("regexp: a new pattern replaces the old one wholesale (#452)") {
    RegexpRig rig(R"(\d+ N)");
    rig.Feed("a1b");
    CHECK(rig.substitution.count() == 1);

    // No substitution this time — the old one must not leak into the new
    // parameter set. (In a live patcher SetParams builds a fresh object, so
    // this only matters for standalone use — which is exactly what a binding
    // driving pObject directly does.)
    rig.op.SetParams(R"([a-z]+)");
    CHECK_FALSE(rig.op.HasSubstitution());
    rig.Feed("a1b");
    CHECK(rig.substitution.silent());
    REQUIRE(rig.match.count() == 2);
    CHECK(rig.match.values[0] == "a");
  }

  TEST_CASE("regexp: a bad pattern after a good one leaves the object inert (#452)") {
    RegexpRig rig("abc");
    REQUIRE(rig.op.Valid());
    rig.op.SetParams("(abc");
    CHECK_FALSE(rig.op.Valid());
    rig.Feed("abc");
    CHECK(rig.match.silent());
    CHECK(rig.nomatch.count() == 1);
  }

  TEST_CASE("regexp: params survive a DumpJSON / ParseJSON round trip (#452)") {
    const std::string params = R"(^/synth/(\d+)/(\w+)$ %2 for %1)";

    YSE::patcher src;
    src.create(2);
    REQUIRE(src.CreateObject(YSE::OBJ::G_REGEXP, params) != nullptr);
    const std::string json = src.DumpJSON();
    CHECK(json.find(".regexp") != std::string::npos);

    YSE::patcher loaded;
    loaded.create(2);
    loaded.ParseJSON(json);
    REQUIRE(loaded.Objects() == 1);

    YSE::pHandle* h = loaded.GetHandleFromList(0);
    REQUIRE(h != nullptr);
    CHECK(std::string(h->Type()) == std::string(".regexp"));
    CHECK(h->GetParams() == params);
    CHECK(h->GetInputs() == 1);
    CHECK(h->GetOutputs() == 4);
  }

} // TEST_SUITE

#include "gCase.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gCaseBase

namespace {

  // Hand-rolled rather than std::tolower / std::toupper, for the two reasons
  // the rest of this family gives for hand-rolling its character tests: those
  // read locale state another thread may be mutating, and they are undefined
  // for a negative char — which every byte of a UTF-8 sequence is on a platform
  // where char is signed. These two are pure, branch on a value range, and are
  // defined for all 256 byte values.
  //
  // Only A-Z / a-z move. Every byte of a multi-byte UTF-8 sequence is 0x80 or
  // above and so is left exactly as it was, which is what makes a byte-wise
  // fold safe on text that is not ASCII rather than merely blind to it.
  inline char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }

  inline char ToUpperAscii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }

} // namespace

gCaseBase::gCaseBase(caseDirection fold) : pObject(false), direction(fold) {
  // The message to fold. Every kind: a list is what gets folded, and a bang, an
  // int and a float pass through, because the fold is the identity on a message
  // with no letters in it and an object that is transparent by definition must
  // not be the one in a chain that drops or retypes a message.
  ADD_IN_0;
  REG_BANG_IN(FoldBang);
  REG_INT_IN(FoldInt);
  REG_FLOAT_IN(FoldFloat);
  REG_LIST_IN(FoldList);

  // ANY: a folded message leaves as a list, but a bang, an int and a float
  // leave as themselves, and a reader should not have to know which arrived to
  // wire to this object.
  ADD_OUT_ANY;

  // The one allocation a fold would otherwise need, taken here on the control
  // thread. The transform is length-preserving, so this covers the result of
  // any message the patcher's queues can deliver.
  outText.reserve(TEXT_CAPACITY);

  ADD_CATEGORY(pCategory::GENERIC);
}

void gCaseBase::Document(const char* summary, const char* inDoc, const char* outDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", inDoc, "any");
  OUTLET_DOC(0, "out", outDoc, "");
}

void gCaseBase::Fold(const char* text, std::size_t length, YSE::THREAD thread) {
  // Filled immediately before the send rather than kept between them: the send
  // path is synchronous, so a patch looping the outlet back into the inlet
  // re-enters this function inside the SendList below. Appends only, into
  // capacity reserved at construction.
  outText.clear();

  // The direction is fixed for the object's lifetime, so the choice is made
  // once here rather than once per byte.
  if (direction == caseDirection::Lower) {
    for (std::size_t i = 0; i < length; i++)
      outText.push_back(ToLowerAscii(text[i]));
  } else {
    for (std::size_t i = 0; i < length; i++)
      outText.push_back(ToUpperAscii(text[i]));
  }

  // An empty message is forwarded rather than swallowed: it is what arrived,
  // not something this object produced out of nothing, and a wire that ate it
  // would not be a wire. See the class documentation.
  outputs[0].SendList(outText, thread);
}

BANG_IN(FoldBang) {
  // Registered on inlet 0 alone, so there is nothing to guard against here.
  (void)inlet;
  // Nothing to fold, and the type is preserved: a bang that came back as the
  // symbol `bang` would be a conversion nobody asked this object for.
  outputs[0].SendBang(thread);
}

INT_IN(FoldInt) {
  (void)inlet;
  // The digits of a number have no case, so the fold is the identity — and it
  // stays an int, so the numeric objects downstream keep working.
  outputs[0].SendInt(value, thread);
}

FLOAT_IN(FoldFloat) {
  (void)inlet;
  outputs[0].SendFloat(value, thread);
}

LIST_IN(FoldList) {
  (void)inlet;
  Fold(value.c_str(), value.size(), thread);
}

#undef className

// ─── the two directions ───────────────────────────────────────────────────────

gToLower::gToLower() : gCaseBase(caseDirection::Lower) {
  Document(
      "Converts the text of a message to lower case — Max's string.tolower. Every A-Z becomes the "
      "matching a-z and nothing else about the message changes: the same bytes, the same length, "
      "the same tokens, the same whitespace. It exists because every comparison in this patcher is "
      "exact — .route, .sel, .match, .substitute and the named-bus objects all compare text "
      "character for character — so a patch handed a name from outside itself, in whatever case a "
      "user or a host application typed it, had no way to match it against a name written into the "
      "patch. A .tolower in front of a .route makes that route case-insensitive without the route "
      "needing a second way to compare, and folding both sides is how case-insensitive matching is "
      "done everywhere else. Until now nothing in the patcher could change the case of a character "
      "at all; the only way to express it was .spell into arithmetic into .itoa, an "
      "object-per-character patch for what is one comparison of a byte. The fold is ASCII only, "
      "which is both a deliberate scope and a guarantee: real case mapping is language-dependent "
      "(Turkish dotless i, German sharp s, Greek final sigma), is not length-preserving, and needs "
      "a table or locale state that no inlet-driven path can afford — while an ASCII fold provably "
      "cannot corrupt text that is not ASCII, since every byte of a multi-byte UTF-8 sequence is "
      "0x80 or above and the folded range is entirely below it. Accented letters, CJK text and raw "
      "binary therefore pass through byte for byte. Whitespace is deliberately left alone, unlike "
      ".tosymbol and .spell which normalise it: those rebuild the message and must decide what its "
      "spacing is, whereas this one rewrites characters in place, and a fold that also changed the "
      "token structure would break the very .route it was put there to feed. An empty message is "
      "forwarded rather than swallowed, and a bang, an int and a float pass straight through in "
      "their own type, since the fold is the identity on a message with no letters in it. "
      "Calculate() does nothing, and no message path allocates, locks or blocks: the fold is one "
      "walk of the bytes into a buffer reserved at construction for the longest list the patcher's "
      "queues carry.",
      "The message to fold. A list has every A-Z in its text replaced by the matching a-z, with "
      "every other byte — digits, punctuation, whitespace and anything outside ASCII — left "
      "exactly as it was. A bang, an int and a float pass straight through in their own type, "
      "since a message with no letters in it is already lower case.",
      "The folded message, as a list, with the token structure and the whitespace of the message "
      "that arrived. A bang, an int or a float leaves as itself rather than as the text that "
      "spells it.");
}

gToUpper::gToUpper() : gCaseBase(caseDirection::Upper) {
  Document(
      "Converts the text of a message to upper case — Max's string.toupper. The mirror of "
      ".tolower: every a-z becomes the matching A-Z and nothing else about the message changes — "
      "the same bytes, the same length, the same tokens, the same whitespace. It exists because "
      "every comparison in this patcher is exact — .route, .sel, .match, .substitute and the "
      "named-bus objects all compare text character for character — so a patch handed a name from "
      "outside itself, in whatever case a user or a host application typed it, had no way to match "
      "it against a name written into the patch. Folding both sides to the same case before the "
      "comparison is how that is done, and which case is chosen only has to be agreed on by both "
      "ends; .toupper is the one to reach for when the names a patch is matching against are "
      "themselves written in capitals, so the fold is applied to the incoming text rather than to "
      "the patch. The fold is ASCII only, which is both a deliberate scope and a guarantee: real "
      "case mapping is language-dependent (Turkish dotless i, German sharp s, Greek final sigma), "
      "is not length-preserving, and needs a table or locale state that no inlet-driven path can "
      "afford — while an ASCII fold provably cannot corrupt text that is not ASCII, since every "
      "byte of a multi-byte UTF-8 sequence is 0x80 or above and the folded range is entirely below "
      "it. Accented letters, CJK text and raw binary therefore pass through byte for byte, and in "
      "particular a lower-case accented letter is left alone rather than half-converted. "
      "Whitespace is deliberately left alone, unlike .tosymbol and .spell which normalise it: "
      "those rebuild the message and must decide what its spacing is, whereas this one rewrites "
      "characters in place, and a fold that also changed the token structure would break the very "
      ".route it was put there to feed. An empty message is forwarded rather than swallowed, and a "
      "bang, an int and a float pass straight through in their own type, since the fold is the "
      "identity on a message with no letters in it. Calculate() does nothing, and no message path "
      "allocates, locks or blocks: the fold is one walk of the bytes into a buffer reserved at "
      "construction for the longest list the patcher's queues carry.",
      "The message to fold. A list has every a-z in its text replaced by the matching A-Z, with "
      "every other byte — digits, punctuation, whitespace and anything outside ASCII — left "
      "exactly as it was. A bang, an int and a float pass straight through in their own type, "
      "since a message with no letters in it is already upper case.",
      "The folded message, as a list, with the token structure and the whitespace of the message "
      "that arrived. A bang, an int or a float leaves as itself rather than as the text that "
      "spells it.");
}

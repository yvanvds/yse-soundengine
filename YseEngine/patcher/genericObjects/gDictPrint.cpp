// `.dict.print` (issue #776). See gDictPrint.h for the design; this file is
// the snapshot, the bounded JSON emitter, and gPrint's budget-and-post
// machinery with a dictionary document flowing through it.
#include "gDictPrint.h"

#include <cstring>

#include "../../internal/rtLogQueue.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

#define className gDictPrint

namespace {

  // What is written when a row did not fit the record. Three characters,
  // which is why the cut leaves room for exactly three — gPrint's mark.
  constexpr std::size_t kEllipsisLength = 3;

  // Two spaces of indent per open brace: the document has to be readable in a
  // log that prefixes every line, so deep stairs would spend the record.
  constexpr std::size_t kIndentStep = 2;

  constexpr char kInletDoc[] =
      "A bang dumps the bound dictionary to the engine log as a nested multi-line JSON document, "
      "one log line per row, each prefixed with the dictionary's name. \"dictionary <name>\" does "
      "the same when it names the dictionary bound by the creation argument — the message a "
      ".dict's reference outlet emits on a bang, so wiring that outlet here gives Max's own "
      "gesture: bang the dict, read its contents. A reference naming anything else, any other "
      "message, or a trigger arriving while a dump is already being composed is refused and "
      "counted rather than logged, since this inlet may be the audio thread. Nothing is logged "
      "from here either: every row is composed into a buffer the object owns and pushed onto the "
      "bounded lock-free queue .print built, so the document reaches the log on the host's next "
      "System::update() tick. A dump that runs past this object's per-tick line budget is cut and "
      "the first refusal posts one notice line, and a row longer than the queue's record is cut "
      "and marked with a trailing '...' — a truncated dump must read as a marked gap, never as a "
      "complete dictionary.";

  constexpr char kNameDoc[] =
      "The dictionary's shared name, addressed as \"<patcherName>.<name>\" — the dictionary a "
      ".dict of the same name in this patcher holds. Resolved once, on the control thread, which "
      "is why no message re-points it at run time. Also the label in front of every printed "
      "line, which is what makes two of these in one patch tellable apart; a name longer than 32 "
      "characters labels with its first 32. Empty binds a private, empty dictionary — the dump "
      "is then '{}' under the label 'dict.print'.";

  constexpr char kLimitDoc[] =
      "The most lines this object emits between two drain ticks — .print's budget, with a wider "
      "default because the unit of output here is a whole document rather than a line: 64 covers "
      "a dictionary of a few dozen entries. Clamped to 1-256, 256 being the shared queue's own "
      "capacity, past which asking for more cannot buy anything. A dump that runs past the "
      "budget is truncated and the first refusal in a tick posts a notice line, so a cut dump is "
      "visible rather than silent; every refusal is counted. A non-numeric second argument "
      "leaves the default standing rather than failing, since a creation argument arriving from "
      "a saved patch must never be able to break loading it.";

} // namespace

CONSTRUCT() {
  // One inlet, as in Max. No int or float handler: a bare number names no
  // dictionary — the family's rule.
  ADD_IN_0;
  REG_BANG_IN(BangIn);
  REG_LIST_IN(ListIn);

  // No outlets. .print's shape, and the right one: an outlet would turn a
  // probe into a participant, and a patch that had to wire this object's
  // output somewhere could no longer leave it in place once it was working.

  ADD_PARAM(dictName);
  ADD_PARAM(budgetArgs);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // A private, empty store to start with, so `store` is never null and no
  // message handler needs a null check. Rebind() trades it for a shared one
  // as soon as there is both a name and a patcher to prefix it with.
  Rebind();

  // Build the queue here, on the control thread, rather than letting the
  // first row this object posts allocate its ring on a render thread —
  // gPrint's one constructor-side effect, for gPrint's reason.
  (void)YSE::INTERNAL::RtLog().capacity();

  ADD_DESCRIPTION(
      "Prints a dictionary's contents to the engine log — Max's dict.print ('print the contents "
      "of a dictionary in the Max Console') on the name-addressed value model .dict settled. "
      "There is no Max window here, so the destination is the engine log, .print's destination: "
      "the thing a host already reads, redirects with YSE::Log().setLogfile() and intercepts "
      "with YSE::Log().setHandler(). It is the debugging instrument for structured data and the "
      "counterpart of .print — a dictionary is the one patcher value a patch cannot see by "
      "wiring it to a sink, because what a cord carries is only its name. The dictionary is "
      "bound from the first creation argument, \".dict.print <name>\", resolved once on the "
      "control thread; a bang, or the dictionary's reference message \"dictionary <name>\", "
      "dumps the whole dictionary as a nested multi-line JSON document — the same document a "
      "saved patch holds, with \"::\" paths expanded into real nesting, entries grouped in "
      "storage order of first appearance, and values typed by the patcher's own classifier: a "
      "single numeric token prints as a number, a multi-token value as an array, anything else "
      "as a string. Each line is prefixed with the dictionary's name, so two of these in one "
      "patch are tellable apart. There are no outlets — a probe, not a participant. Nothing is "
      "ever logged from the message path: the dump is a snapshot of the dictionary as it stood "
      "at the trigger, composed row by row into buffers the object owns and pushed onto the "
      "bounded lock-free queue .print built, whose control-thread drain hands the lines to the "
      "log on the host's next tick. Nothing on any message path allocates, locks or blocks, and "
      "Calculate() does nothing. Two bounds can refuse a line and both report: this object's "
      "per-tick budget — the second creation argument, default 64, clamped to 1-256 — whose "
      "first refusal in a tick posts a notice, and the shared queue's capacity, whose overflow "
      "the next drain names as a single count. A row longer than the record is cut and marked "
      "with '...'. Only the creation arguments persist across a save; the lines already sent "
      "belong to the log rather than to the patch.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "dump", kInletDoc, "");
  PARAM_DOC("name", "", kNameDoc, "any identifier");
  PARAM_DOC("lines", "64", kLimitDoc, "1-256");
}

// ─── creation arguments ───────────────────────────────────────────────────────

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is what makes SetParams("") a real reset —
// dropping the name and the budget, going back to a private dictionary.
// gDict's rule plus gPrint's.
PARM_CLEAR() {
  dictName.clear();
  budgetArgs.clear();
  linesPerTick = DEFAULT_LINES_PER_TICK;
  Rebind();
}

PARM_PARSE() {
  linesPerTick = DEFAULT_LINES_PER_TICK;

  // The budget. Read strictly — ReadNumericToken answers "is this token a
  // number at all" — and a token that is not one leaves the default standing
  // rather than throwing, which is what stops a hand-edited or newer saved
  // patch from failing to load. gPrint's reading of the same argument.
  for (const std::string& token : budgetArgs) {
    if (token.empty()) continue;
    float number = 0.f;
    if (ReadNumericToken(token, number)) {
      int asked = ExprToInt(number);
      if (asked < MIN_LINES_PER_TICK) asked = MIN_LINES_PER_TICK;
      if (asked > MAX_LINES_PER_TICK) asked = MAX_LINES_PER_TICK;
      linesPerTick = asked;
    }
    break;
  }

  Rebind();
}

// `parent` is a patcherImplementation by construction (the patcher hands
// itself to every object via SetParent); the cast mirrors gDict's.
void gDictPrint::SetParent(pObject* newParent) {
  pObject::SetParent(newParent);
  Rebind();
}

void gDictPrint::RefreshBinding() {
  Rebind();
}

void gDictPrint::Rebind() {
  // No name, or no patcher to prefix it with, means no address — and no
  // address means a private, empty dictionary. See gDict.h for why an
  // unnamed object does not pool on "<patcherName>.".
  std::string address;
  if (!dictName.empty() && parent != nullptr) {
    auto* p = static_cast<patcherImplementation*>(parent);
    address = p->Name() + "." + dictName;
  }

  // Unchanged binding: keep the store. A live SetParams that leaves the name
  // alone must not re-anchor it, and neither must the second Rebind() a
  // Set() makes (clear, then parse).
  if (store != nullptr && address == boundAddress) return;

  bool created = false;
  store = address.empty() ? std::make_shared<dictStore>()
                          : AcquireNamedStore<dictStore>(address, created);
  boundAddress = address;
}

// ─── messages ─────────────────────────────────────────────────────────────────

BANG_IN(BangIn) {
  (void)inlet;
  (void)thread;
  Dump();
}

LIST_IN(ListIn) {
  (void)inlet;
  (void)thread;
  // The dictionary's reference triggers the dump — the message its .dict
  // emits on a bang. Anything else, including a reference to a dictionary
  // this object is not bound to, is refused: resolving an unrecognised name
  // means the registry's mutex, and this may be the audio thread.
  if (DictReferenceNames(value, dictName)) {
    Dump();
    return;
  }
  Refuse();
}

// ─── the dump ─────────────────────────────────────────────────────────────────

void gDictPrint::Dump() {
  // The re-entrancy guard, held across snapshot and emission: two threads
  // dumping into one emitter would interleave their rows into one broken
  // document. The loser is dropped and counted rather than made to spin,
  // this being a path the audio callback takes. No outlet exists, so no
  // cord can loop back into the guard from inside the dump.
  if (busy.exchange(true, std::memory_order_acq_rel)) {
    Refuse();
    return;
  }

  // The snapshot: the dictionary as it stands right now, copied out under
  // its guard into rows reserved at construction — bounded assigns, no
  // allocation. Released before any line is posted, so the store is never
  // held while the queue is being fed and a concurrent `set` never loses its
  // try-lock to a printout. gDictIter's move, and what makes the dump one
  // consistent document.
  {
    const dictStoreGuard guard(store->busy);
    if (!guard.Held()) {
      Refuse();
      busy.store(false, std::memory_order_release);
      return;
    }
    snapshot.count = store->count;
    for (std::size_t i = 0; i < store->count; i++) {
      snapshot.entries[i].key.assign(store->entries[i].key);
      snapshot.entries[i].value.assign(store->entries[i].value);
    }
  }

  std::memset(consumed, 0, sizeof(bool) * snapshot.count);

  // The root opener, held back: an empty dictionary closes it into "{}" on
  // one line rather than an opener with nothing to open.
  pending[0] = '{';
  pendingLength = 1;
  pendingOpen = true;

  EmitLevel(0, 1);

  // Close the root. A still-open root absorbed no member, so it closes in
  // place; otherwise the last member flushes without a comma and the closer
  // is a line of its own at indent zero.
  if (pendingOpen) {
    pending[pendingLength++] = '}';
    pendingOpen = false;
  } else {
    FlushPending(false);
    pending[0] = '}';
    pendingLength = 1;
  }
  FlushPending(false);

  busy.store(false, std::memory_order_release);
}

void gDictPrint::EmitLevel(std::size_t prefixLength, int depth) {
  for (std::size_t i = 0; i < snapshot.count; i++) {
    if (consumed[i]) continue;
    const std::string& key = snapshot.entries[i].key;
    if (key.size() < prefixLength) continue;
    if (prefixLength > 0 && std::memcmp(key.data(), prefix, prefixLength) != 0) continue;

    // The next path segment: up to the first "::" after the prefix. An empty
    // one — "a::" run out, or "::b" starting on the separator — is not a path
    // this store can express as JSON, so the entry is skipped rather than
    // producing an unnamed member. DictToJson's rule.
    const std::size_t remain = key.size() - prefixLength;
    std::size_t segLen = remain;
    for (std::size_t s = 0; s + 1 < remain; s++) {
      if (key[prefixLength + s] == ':' && key[prefixLength + s + 1] == ':') {
        segLen = s;
        break;
      }
    }
    if (segLen == 0) {
      consumed[i] = true;
      continue;
    }
    const char* seg = key.data() + prefixLength;
    const bool isLeaf = (segLen == remain);

    if (isLeaf) {
      // A leaf, and the earliest entry of its segment (the scan is storage
      // order): it wins over any later-stored "seg::..." sub-tree, which is
      // DictToJson's collision rule — storage order decides, .coll's rule
      // for a duplicate address. The losers are consumed unemitted.
      BeginRow(depth);
      AppendStringJson(seg, segLen);
      AppendText(": ", 2);
      AppendValueJson(snapshot.entries[i].value);
      CommitRow(false);
      consumed[i] = true;

      for (std::size_t j = i + 1; j < snapshot.count; j++) {
        if (consumed[j]) continue;
        const std::string& other = snapshot.entries[j].key;
        if (other.size() < prefixLength + segLen + 2) continue;
        if (std::memcmp(other.data(), key.data(), prefixLength + segLen) != 0) continue;
        if (other[prefixLength + segLen] != ':' || other[prefixLength + segLen + 1] != ':')
          continue;
        consumed[j] = true;
      }
      continue;
    }

    // A branch, and the earliest of its segment: a later-stored leaf of the
    // same name loses to it — the mirror collision, same rule.
    for (std::size_t j = i + 1; j < snapshot.count; j++) {
      if (consumed[j]) continue;
      const std::string& other = snapshot.entries[j].key;
      if (other.size() != prefixLength + segLen) continue;
      if (std::memcmp(other.data(), key.data(), prefixLength + segLen) != 0) continue;
      consumed[j] = true;
    }

    BeginRow(depth);
    AppendStringJson(seg, segLen);
    AppendText(": {", 3);
    CommitRow(true);

    // Descend: the prefix grows by this segment and its separator, and every
    // entry under it — this one included — is consumed by the recursion.
    // Bounded: the prefix never outgrows a key that already fit KEY_CAPACITY,
    // and the depth never exceeds the segments such a key can spell.
    std::memcpy(prefix + prefixLength, seg, segLen);
    prefix[prefixLength + segLen] = ':';
    prefix[prefixLength + segLen + 1] = ':';
    EmitLevel(prefixLength + segLen + 2, depth + 1);

    // Close the branch. A still-open one absorbed no valid member — "a::"
    // alone, say — and closes in place as "seg": {}; otherwise its last
    // member flushes without a comma and the closer takes the opener's
    // indent.
    if (pendingOpen) {
      pending[pendingLength++] = '}';
      pendingOpen = false;
    } else {
      FlushPending(false);
      composeLength = 0;
      const std::size_t indent = kIndentStep * (std::size_t)depth;
      for (std::size_t s = 0; s < indent; s++)
        compose[composeLength++] = ' ';
      compose[composeLength++] = '}';
      CommitRow(false);
    }
  }
}

// ─── composing rows ───────────────────────────────────────────────────────────

void gDictPrint::BeginRow(int depth) {
  // The held-back previous row learns its fate here: a sibling follows, so it
  // takes a comma — unless it is a still-open brace, whose first member never
  // does.
  FlushPending(!pendingOpen);
  pendingOpen = false;

  // depth counts open braces, so members sit one step in from their opener.
  composeLength = 0;
  const std::size_t indent = kIndentStep * (std::size_t)depth;
  for (std::size_t s = 0; s < indent && composeLength < COMPOSE_CAPACITY - 2; s++)
    compose[composeLength++] = ' ';
}

void gDictPrint::CommitRow(bool open) {
  std::memcpy(pending, compose, composeLength);
  pendingLength = composeLength;
  pendingOpen = open;
}

void gDictPrint::FlushPending(bool comma) {
  if (pendingLength == 0) return;
  if (comma && pendingLength < COMPOSE_CAPACITY - 1) pending[pendingLength++] = ',';
  Post(pending, pendingLength);
  pendingLength = 0;
}

void gDictPrint::AppendText(const char* text, std::size_t length) {
  if (composeLength + length > COMPOSE_CAPACITY - 2) return;
  std::memcpy(compose + composeLength, text, length);
  composeLength += length;
}

// ─── values, spelled as JSON ──────────────────────────────────────────────────

void gDictPrint::AppendValueJson(const std::string& value) {
  // A stored value is list text. One token becomes the scalar it spells,
  // several become an array of those, and nothing at all becomes an empty
  // string — which is what `set <key>` with no value stored. DictToJson's
  // classification, walked in place.
  std::size_t i = 0;
  int tokens = 0;
  std::size_t firstBegin = 0;
  std::size_t firstLength = 0;

  while (i < value.size()) {
    while (i < value.size() && IsSelectorSeparator(value[i]))
      i++;
    if (i >= value.size()) break;
    const std::size_t begin = i;
    while (i < value.size() && !IsSelectorSeparator(value[i]))
      i++;
    if (tokens == 0) {
      firstBegin = begin;
      firstLength = i - begin;
    }
    tokens++;
  }

  if (tokens == 0) {
    AppendText("\"\"", 2);
    return;
  }
  if (tokens == 1) {
    AppendTokenJson(value.c_str() + firstBegin, firstLength);
    return;
  }

  AppendText("[", 1);
  i = 0;
  int emitted = 0;
  while (i < value.size()) {
    while (i < value.size() && IsSelectorSeparator(value[i]))
      i++;
    if (i >= value.size()) break;
    const std::size_t begin = i;
    while (i < value.size() && !IsSelectorSeparator(value[i]))
      i++;
    if (emitted > 0) AppendText(", ", 2);
    AppendTokenJson(value.c_str() + begin, i - begin);
    emitted++;
  }
  AppendText("]", 1);
}

void gDictPrint::AppendTokenJson(const char* text, std::size_t length) {
  float parsed = 0.f;
  if (!ReadNumericToken(text, length, parsed)) {
    AppendStringJson(text, length);
    return;
  }

  // A token that is nothing but an optional sign and digits is an integer,
  // and it goes out as its own characters — arbitrary precision, no float
  // detour — normalised only where JSON demands it: no '+', no leading
  // zeros.
  std::size_t p = 0;
  bool negative = false;
  if (text[0] == '+' || text[0] == '-') {
    negative = (text[0] == '-');
    p = 1;
  }
  bool plainInt = p < length;
  for (std::size_t s = p; s < length; s++) {
    if (text[s] < '0' || text[s] > '9') {
      plainInt = false;
      break;
    }
  }
  if (plainInt) {
    while (p + 1 < length && text[p] == '0')
      p++;
    if (negative) AppendText("-", 1);
    AppendText(text + p, length - p);
    return;
  }

  // Anything else the classifier accepted — "5.", ".5", "1e3" — is a float,
  // spelled by the patcher's own formatter, which is allocation-free and
  // locale-free where the C library is neither. Its one non-JSON habit is
  // the trailing point ("120."), which a single '0' repairs.
  char buffer[kExprValueTextMax];
  const int written = ExprFormatValue(ExprValue::Float(parsed), buffer, kExprValueTextMax);
  AppendText(buffer, (std::size_t)written);
  if (written > 0 && buffer[written - 1] == '.') AppendText("0", 1);
}

void gDictPrint::AppendStringJson(const char* text, std::size_t length) {
  if (composeLength >= COMPOSE_CAPACITY - 2) return;
  compose[composeLength++] = '"';
  for (std::size_t s = 0; s < length; s++) {
    const char c = text[s];
    if (composeLength >= COMPOSE_CAPACITY - 8) break;
    switch (c) {
    case '"':
      compose[composeLength++] = '\\';
      compose[composeLength++] = '"';
      break;
    case '\\':
      compose[composeLength++] = '\\';
      compose[composeLength++] = '\\';
      break;
    case '\b':
      compose[composeLength++] = '\\';
      compose[composeLength++] = 'b';
      break;
    case '\f':
      compose[composeLength++] = '\\';
      compose[composeLength++] = 'f';
      break;
    case '\n':
      compose[composeLength++] = '\\';
      compose[composeLength++] = 'n';
      break;
    case '\r':
      compose[composeLength++] = '\\';
      compose[composeLength++] = 'r';
      break;
    case '\t':
      compose[composeLength++] = '\\';
      compose[composeLength++] = 't';
      break;
    default:
      if ((unsigned char)c < 0x20) {
        const char* hex = "0123456789abcdef";
        compose[composeLength++] = '\\';
        compose[composeLength++] = 'u';
        compose[composeLength++] = '0';
        compose[composeLength++] = '0';
        compose[composeLength++] = hex[((unsigned char)c >> 4) & 0xF];
        compose[composeLength++] = hex[(unsigned char)c & 0xF];
      } else {
        compose[composeLength++] = c;
      }
      break;
    }
  }
  if (composeLength < COMPOSE_CAPACITY - 1) compose[composeLength++] = '"';
}

// ─── the queue, through the budget ────────────────────────────────────────────

bool gDictPrint::ClaimBudget() {
  const std::uint64_t now = YSE::INTERNAL::RtLog().tick();

  // A line that finds the counter tagged with an older tick is the first of
  // a new one — gPrint's clock, unchanged, approximate under concurrency on
  // purpose (see gPrint.h: a flood guard, not a quota).
  if (budgetTick.load(std::memory_order_relaxed) != now) {
    budgetTick.store(now, std::memory_order_relaxed);
    usedThisTick.store(0, std::memory_order_relaxed);
  }

  const int used = usedThisTick.load(std::memory_order_relaxed);
  if (used >= linesPerTick) return false;
  usedThisTick.store(used + 1, std::memory_order_relaxed);
  return true;
}

void gDictPrint::PostRaw(const char* body, std::size_t bodyLength) {
  // The whole line on the stack. Nothing here allocates, which is the point:
  // this runs on whichever thread the trigger arrived on.
  char line[YSE::INTERNAL::rtLogQueue::kLineCapacity + 1];
  std::size_t written = 0;

  const char* label = dictName.empty() ? DEFAULT_LABEL : dictName.data();
  std::size_t labelLength = dictName.empty() ? std::strlen(DEFAULT_LABEL) : dictName.size();
  if (labelLength > LABEL_CAPACITY) labelLength = LABEL_CAPACITY;
  std::memcpy(line, label, labelLength);
  written = labelLength;
  line[written++] = ':';
  line[written++] = ' ';

  // At most LABEL_CAPACITY + 2 characters are spoken for, so there is always
  // room for a body and for the ellipsis that marks a cut one.
  const std::size_t room = YSE::INTERNAL::rtLogQueue::kLineCapacity - written;
  if (bodyLength <= room) {
    if (bodyLength > 0) std::memcpy(line + written, body, bodyLength);
    written += bodyLength;
  } else {
    const std::size_t keep = room - kEllipsisLength;
    std::memcpy(line + written, body, keep);
    written += keep;
    line[written++] = '.';
    line[written++] = '.';
    line[written++] = '.';
  }
  line[written] = '\0';

  if (YSE::INTERNAL::RtLog().post(line, written)) {
    posted.fetch_add(1, std::memory_order_relaxed);
  } else {
    // The queue was full. It counts and reports the loss itself; this counter
    // is what lets a test ask *this* object how much it lost.
    dropped.fetch_add(1, std::memory_order_relaxed);
  }
}

void gDictPrint::Post(const char* body, std::size_t bodyLength) {
  if (ClaimBudget()) {
    PostRaw(body, bodyLength);
    return;
  }

  dropped.fetch_add(1, std::memory_order_relaxed);

  // One notice per tick, claimed with a single exchange so exactly one caller
  // writes it however many dumps are flooding. Exempt from the budget it is
  // reporting on, which it would otherwise be the first casualty of.
  const std::uint64_t now = YSE::INTERNAL::RtLog().tick();
  if (noticeTick.exchange(now, std::memory_order_relaxed) == now) return;

  char notice[96];
  std::size_t n = 0;
  const char head[] = "rate limit reached (";
  std::memcpy(notice, head, sizeof(head) - 1);
  n += sizeof(head) - 1;
  n += WriteInt(linesPerTick, notice + n);
  const char tail[] = " per tick); rest of the dump dropped";
  std::memcpy(notice + n, tail, sizeof(tail) - 1);
  n += sizeof(tail) - 1;

  PostRaw(notice, n);
}

#undef className

/*
  ==============================================================================

    filePlaylist.cpp
    A list of sound files played from disk, one clip at a time (issue #1158).

  ==============================================================================
*/

#include "filePlaylist.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace YSE::DSP;

namespace {

  constexpr Flt FOLLOW = std::numeric_limits<Flt>::quiet_NaN();

  // The last part of a path, after its final '/' or '\'.
  std::string LastPart(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
  }

} // namespace

// What the audio thread plays from: the list as the control side last left
// it. Made and freed off the audio thread.
struct filePlaylist::order {
  std::vector<slot> slots;
};

filePlaylist::clip::clip() {
  std::fill(settings, settings + SETTINGS, FOLLOW);
}

// ─── construction ─────────────────────────────────────────────────────────────

filePlaylist::filePlaylist(UInt outputs) : player(outputs), head(STANDARD_BUFFERSIZE) {
  head = 0.f;
}

filePlaylist::~filePlaylist() {
  delete current; // NOLINT(cppcoreguidelines-owning-memory): the handoff's lists
  delete incoming.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
  delete retired.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
}

filePlaylist& filePlaylist::channels(UInt outputs) {
  player.channels(outputs);
  return *this;
}

// ─── the control side ─────────────────────────────────────────────────────────

void filePlaylist::CollectLocked() {
  order* old = retired.exchange(nullptr, std::memory_order_acq_rel);
  delete old; // NOLINT(cppcoreguidelines-owning-memory)
}

void filePlaylist::collect() {
  const std::lock_guard<std::mutex> lock(edit);
  CollectLocked();
  player.collect();
}

void filePlaylist::Publish() {
  CollectLocked();
  auto* next = new order(); // NOLINT(cppcoreguidelines-owning-memory): handed over
  // Only `edit` holders write `slots`, and this is one.
  next->slots = slots;
  // A list the audio thread has not taken yet is replaced: the last one wins.
  order* unread = incoming.exchange(next, std::memory_order_acq_rel);
  delete unread; // NOLINT(cppcoreguidelines-owning-memory)
  changes.fetch_add(1, std::memory_order_acq_rel);
}

Int filePlaylist::FreeCue() const {
  // At most MAX_CLIPS cues are taken, so one of the first MAX_CLIPS + 1 is
  // free.
  std::vector<bool> taken(MAX_CLIPS + 1, false);
  for (const slot& s : slots) {
    const Int k = s.cue - fileStreamCues::FIRST_CUE;
    if (k >= 0 && k <= static_cast<Int>(MAX_CLIPS)) taken[static_cast<std::size_t>(k)] = true;
  }
  Int k = 0;
  while (taken[static_cast<std::size_t>(k)])
    k++;
  return fileStreamCues::FIRST_CUE + k;
}

// The span the cue table made of the slot's cue.
bool filePlaylist::Read(slot& s) const {
  for (const fileStreamCues::cue& c : player.cueList()->cues()) {
    if (c.number != s.cue) continue;
    s.c.startMs = c.startMs;
    s.c.endMs = c.endMs;
    return true;
  }
  return false;
}

Int filePlaylist::append(const std::string& file, Int at, const std::string& name) {
  if (file.empty()) return 0;
  const std::lock_guard<std::mutex> lock(edit);
  if (slots.size() >= MAX_CLIPS) return 0;
  slot s;
  s.cue = FreeCue();
  // Checks the header and holds the whole file: blocks on the disk.
  if (!player.cueList()->preload(s.cue, file)) return 0;
  s.c.file = file;
  s.c.name = name.empty() ? LastPart(file) : name;
  Read(s);
  s.c.lengthMs = s.c.endMs;
  // Opens the clip's stream and reads its first buffer.
  player.sync();
  const auto size = static_cast<Int>(slots.size());
  const Int place = at >= 1 && at <= size ? at : size + 1;
  {
    const std::lock_guard<std::mutex> shown(view);
    slots.insert(slots.begin() + (place - 1), std::move(s));
  }
  Publish();
  return place;
}

bool filePlaylist::remove(Int number) {
  const std::lock_guard<std::mutex> lock(edit);
  if (number < 1 || number > static_cast<Int>(slots.size())) return false;
  player.cueList()->remove(slots[static_cast<std::size_t>(number - 1)].cue);
  player.sync();
  {
    const std::lock_guard<std::mutex> shown(view);
    slots.erase(slots.begin() + (number - 1));
  }
  Publish();
  return true;
}

UInt filePlaylist::clear() {
  const std::lock_guard<std::mutex> lock(edit);
  const auto removed = static_cast<UInt>(slots.size());
  if (removed == 0) return 0;
  player.cueList()->clear();
  player.sync();
  {
    const std::lock_guard<std::mutex> shown(view);
    slots.clear();
  }
  Publish();
  return removed;
}

bool filePlaylist::move(Int from, Int to) {
  const std::lock_guard<std::mutex> lock(edit);
  const auto size = static_cast<Int>(slots.size());
  if (from < 1 || from > size || to < 1 || to > size) return false;
  if (from != to) {
    const std::lock_guard<std::mutex> shown(view);
    auto a = slots.begin() + (from - 1);
    auto b = slots.begin() + (to - 1);
    if (from < to) {
      std::rotate(a, a + 1, b + 1);
    } else {
      std::rotate(b, a, a + 1);
    }
  }
  Publish();
  return true;
}

bool filePlaylist::Span(Int number, Flt start, Flt end, bool normalized, bool whole) {
  const std::lock_guard<std::mutex> lock(edit);
  const auto size = static_cast<Int>(slots.size());
  if (number < 0 || number > size || size == 0) return false;
  if (!whole && (!std::isfinite(start) || !std::isfinite(end))) return false;
  const Int first = number == 0 ? 1 : number;
  const Int last = number == 0 ? size : number;
  std::shared_ptr<fileStreamCues> table = player.cueList();
  bool ok = true;
  bool changed = false;
  for (Int n = first; n <= last; ++n) {
    slot s = slots[static_cast<std::size_t>(n - 1)];
    // The header again, to convert the span: blocks on the disk.
    const bool set = whole ? table->preload(s.cue, s.c.file)
                           : table->preload(s.cue, s.c.file, start, end, 1.f, normalized);
    if (!set) {
      ok = false;
      continue;
    }
    Read(s);
    s.c.selected = !whole;
    {
      const std::lock_guard<std::mutex> shown(view);
      slots[static_cast<std::size_t>(n - 1)] = std::move(s);
    }
    changed = true;
  }
  if (changed) {
    // A clip whose span changed gets a new stream; the others keep theirs.
    player.sync();
    Publish();
  }
  return ok;
}

bool filePlaylist::selection(Int number, Flt start, Flt end) {
  return Span(number, start, end, true, false);
}

bool filePlaylist::selectionMs(Int number, Flt startMs, Flt endMs) {
  return Span(number, startMs, endMs, false, false);
}

bool filePlaylist::clearSelection(Int number) {
  return Span(number, 0.f, 0.f, false, true);
}

bool filePlaylist::setting(Int number, SETTING which, Flt value) {
  if (which >= SETTINGS) return false;
  if (!std::isnan(value)) {
    if (!std::isfinite(value)) return false;
    switch (which) {
    case LOOP:
    case TIMESTRETCH:
      value = value != 0.f ? 1.f : 0.f;
      break;
    case PITCHSHIFT:
      if (value <= 0.f) return false;
      break;
    case QUALITY:
      value = std::floor(value);
      if (value < static_cast<Flt>(pitchShifter::BASIC) ||
          value > static_cast<Flt>(pitchShifter::BEST))
        return false;
      break;
    default:
      break;
    }
  }
  const std::lock_guard<std::mutex> lock(edit);
  const auto size = static_cast<Int>(slots.size());
  if (number < 0 || number > size || size == 0) return false;
  const Int first = number == 0 ? 1 : number;
  const Int last = number == 0 ? size : number;
  {
    const std::lock_guard<std::mutex> shown(view);
    for (Int n = first; n <= last; ++n)
      slots[static_cast<std::size_t>(n - 1)].c.settings[which] = value;
  }
  Publish();
  return true;
}

std::vector<filePlaylist::clip> filePlaylist::clips() const {
  const std::lock_guard<std::mutex> lock(view);
  std::vector<clip> out;
  out.reserve(slots.size());
  for (const slot& s : slots)
    out.push_back(s.c);
  return out;
}

UInt filePlaylist::count() const {
  const std::lock_guard<std::mutex> lock(view);
  return static_cast<UInt>(slots.size());
}

// ─── the audio side ───────────────────────────────────────────────────────────

void filePlaylist::Take() {
  // Only while the retired slot is empty, so the old list has somewhere to
  // go.
  if (retired.load(std::memory_order_acquire) != nullptr) return;
  if (incoming.load(std::memory_order_relaxed) == nullptr) return;
  order* next = incoming.exchange(nullptr, std::memory_order_acq_rel);
  if (next == nullptr) return;
  retired.store(current, std::memory_order_release);
  current = next;
}

Int filePlaylist::Index(Int cue) const {
  if (current == nullptr || cue <= 0) return -1;
  const auto& slots = current->slots;
  for (std::size_t i = 0; i < slots.size(); ++i)
    if (slots[i].cue == cue) return static_cast<Int>(i);
  return -1;
}

UInt filePlaylist::clipCount() const {
  return current != nullptr ? static_cast<UInt>(current->slots.size()) : 0;
}

const filePlaylist::clip* filePlaylist::clipAt(Int number) const {
  if (number < 1 || number > static_cast<Int>(clipCount())) return nullptr;
  return &current->slots[static_cast<std::size_t>(number - 1)].c;
}

filePlaylist& filePlaylist::play(Int number) {
  if (number <= 0) return stop();
  request = REQUEST::PLAY;
  requestClip = number;
  return *this;
}

filePlaylist& filePlaylist::next() {
  request = REQUEST::NEXT;
  return *this;
}

filePlaylist& filePlaylist::stop() {
  request = REQUEST::STOP;
  return *this;
}

filePlaylist& filePlaylist::pause() {
  request = REQUEST::PAUSE;
  return *this;
}

filePlaylist& filePlaylist::resume() {
  request = REQUEST::RESUME;
  return *this;
}

filePlaylist& filePlaylist::speed(Flt value) {
  if (std::isfinite(value)) shared[SPEED] = value;
  return *this;
}

filePlaylist& filePlaylist::pitchShift(Flt ratio) {
  if (std::isfinite(ratio) && ratio > 0.f) shared[PITCHSHIFT] = ratio;
  return *this;
}

filePlaylist& filePlaylist::pitchShiftCents(Flt cents) {
  if (std::isfinite(cents)) shared[PITCHSHIFTCENT] = cents;
  return *this;
}

filePlaylist& filePlaylist::quality(pitchShifter::QUALITY value) {
  if (value >= pitchShifter::BASIC && value <= pitchShifter::BEST)
    shared[QUALITY] = static_cast<Flt>(value);
  return *this;
}

// The settings of the clip of `cue`: its own, else the playlist's.
void filePlaylist::Apply(Int cue) {
  const Int i = Index(cue);
  const slot* s = i >= 0 ? &current->slots[static_cast<std::size_t>(i)] : nullptr;
  auto value = [&](SETTING k) {
    if (s != nullptr && !std::isnan(s->c.settings[k])) return s->c.settings[k];
    return shared[k];
  };
  player.loop(false)
      .loopOne(s != nullptr && value(LOOP) != 0.f ? cue : 0)
      .speed(value(SPEED))
      .pitchShift(value(PITCHSHIFT))
      .pitchShiftCents(value(PITCHSHIFTCENT))
      .timeStretch(value(TIMESTRETCH) != 0.f)
      .quality(static_cast<pitchShifter::QUALITY>(static_cast<int>(value(QUALITY))));
}

YSE::DSP::buffer& filePlaylist::operator()(UInt length) {
  Take();
  startedClip = 0;
  doneClip = 0;
  loopedClip = 0;
  dueProgress = false;

  // The transport call since the last block.
  const REQUEST what = request;
  request = REQUEST::NONE;
  bool stopping = false;
  switch (what) {
  case REQUEST::PLAY:
    if (requestClip >= 1 && requestClip <= static_cast<Int>(clipCount()))
      pendingCue = current->slots[static_cast<std::size_t>(requestClip - 1)].cue;
    break;
  case REQUEST::NEXT: {
    const auto count = static_cast<Int>(clipCount());
    if (count > 0) {
      const Int from = Index(soundingCue != 0 ? soundingCue : lastCue);
      const Int to = from < 0 ? 0 : (from + 1) % count;
      pendingCue = current->slots[static_cast<std::size_t>(to)].cue;
    }
    break;
  }
  case REQUEST::STOP:
    pendingCue = 0;
    player.stop();
    stopping = true;
    break;
  case REQUEST::PAUSE:
    player.pause();
    break;
  case REQUEST::RESUME:
    player.resume();
    break;
  default:
    break;
  }

  // A clip to start: asked again every block until the player has its
  // stream (a clip just added, or whose stream a new selection replaced),
  // unless it left the list.
  if (pendingCue != 0 && Index(pendingCue) < 0) pendingCue = 0;
  if (pendingCue != 0) player.play(pendingCue);
  Apply(pendingCue != 0 ? pendingCue : soundingCue);

  player(length);
  length = player.out(0).getLength();

  const Int after = player.cue();
  Int sounding = soundingCue;
  // The player took the play: it has the stream it was asked for.
  if (pendingCue != 0 && player.hasCue(pendingCue)) {
    startedClip = Index(pendingCue) + 1;
    sounding = pendingCue;
    pendingCue = 0;
  }
  if (player.done()) {
    doneClip = Index(sounding) + 1;
  } else if (!stopping && sounding != 0 && after == 0 && startedClip == 0) {
    // Its stream went without an end: the clip left the list, or a new
    // selection replaced the stream, and then it plays again from there.
    if (Index(sounding) >= 0) pendingCue = sounding;
  }
  if (after != 0 && player.looped() > 0) loopedClip = Index(after) + 1;

  // The playhead: the clip number plus how far into its file.
  if (head.getLength() != length) head.resize(length);
  Flt* h = head.getPtr();
  const Int shown = Index(after);
  Flt fraction = 0.f;
  if (shown < 0) {
    std::fill(h, h + length, 0.f);
    through = 0.f;
    progressWait = 0.0;
  } else {
    const clip& c = current->slots[static_cast<std::size_t>(shown)].c;
    const auto base = static_cast<Flt>(shown + 1);
    // Below the next clip's number, however large the number.
    const Flt top = std::nextafter(base + 1.f, base);
    const Flt scale = c.lengthMs > 0.f ? 1.f / c.lengthMs : 0.f;
    const Flt* w = player.position().getPtr();
    for (UInt i = 0; i < length; ++i) {
      const Flt v = base + w[i] * scale;
      h[i] = v < top ? v : top;
    }
    const Flt at = length > 0 ? w[length - 1] : 0.f;
    fraction = length > 0 ? h[length - 1] - base : 0.f;
    const Flt span = c.endMs - c.startMs;
    through = span > 0.f ? std::min(1.f, std::max(0.f, (at - c.startMs) / span)) : 0.f;

    if (startedClip != 0) progressWait = 0.0;
    if (reporting && !player.paused()) {
      progressWait -= static_cast<Dbl>(length);
      if (progressWait <= 0.0) {
        dueProgress = true;
        const Dbl rate = YSE::SAMPLERATE > 0 ? static_cast<Dbl>(YSE::SAMPLERATE) : 44100.0;
        const Dbl interval = static_cast<Dbl>(PROGRESS_MS) * rate / 1000.0;
        progressWait += interval;
        if (progressWait <= 0.0) progressWait = interval;
      }
    }
  }

  // A clip that started and ended in this block counts as the last one too.
  if (sounding != 0) lastCue = sounding;
  if (after != 0) lastCue = after;
  soundingCue = after;
  shownClip.store(shown + 1, std::memory_order_relaxed);
  shownPaused.store(shown >= 0 && player.paused(), std::memory_order_relaxed);
  shownWhere.store(fraction, std::memory_order_relaxed);
  return player.out(0);
}

/*
  ==============================================================================

    fileStreamPlayer.cpp
    Plays sound files streamed from disk, with cues (issue #1155).

  ==============================================================================
*/

#include "fileStreamPlayer.hpp"
#include "../../internalHeaders.h"
#include "../../internal/customFileReader.h"
#include "../../io.hpp"
#include <algorithm>
#include <cmath>
#include <sndfile.hh>
#include <unordered_map>

using namespace YSE::DSP;

namespace {

  // ─── the registry of cue tables ─────────────────────────────────────────────

  struct cueRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<fileStreamCues>> names;
  };

  cueRegistry& Tables() {
    // Never destroyed, so a table freed during static destruction still finds
    // its registry.
    static cueRegistry* const instance =
        new cueRegistry(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  // A sound file's header, through the host's virtual file system when it is
  // active, as DSP::sampleBuffer::read opens one.
  struct header {
    SndfileHandle handle;
    void* ioHandle = nullptr;

    explicit header(const std::string& path) {
      if (YSE::IO().getActive()) {
        long long size = 0;
        if (!YSE::INTERNAL::customFileReader::Open(path.c_str(), &size, &ioHandle)) return;
        handle = SndfileHandle(YSE::INTERNAL::customFileReader::GetVIO(), ioHandle);
      } else {
        handle = SndfileHandle(path);
      }
    }
    ~header() {
      // The handle reads through ioHandle, so it closes first.
      handle = SndfileHandle();
      if (ioHandle != nullptr) YSE::INTERNAL::customFileReader::Close(ioHandle);
    }
    header(const header&) = delete;
    header& operator=(const header&) = delete;
    header(header&&) = delete;
    header& operator=(header&&) = delete;
  };

  Long ClampFrames(Long value, Long low, Long high) {
    return value < low ? low : (value > high ? high : value);
  }

  // Milliseconds at `rate` Hz in whole frames.
  Long MsToFrames(Flt ms, Int rate) {
    return static_cast<Long>(std::floor(static_cast<Dbl>(ms) * static_cast<Dbl>(rate) / 1000.0));
  }

} // namespace

// ─── fileStreamCues ───────────────────────────────────────────────────────────

fileStreamCues::fileStreamCues(std::string name) : tableName(std::move(name)) {}

fileStreamCues::~fileStreamCues() {
  if (tableName.empty()) return;
  cueRegistry& reg = Tables();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(tableName);
  // Only our own, expired entry: acquire() may already have given the name to
  // a new table while this one was on its way out.
  if (it != reg.names.end() && it->second.expired()) reg.names.erase(it);
}

std::shared_ptr<fileStreamCues> fileStreamCues::acquire(const std::string& name) {
  // Not make_shared: the constructor is private.
  if (name.empty()) return std::shared_ptr<fileStreamCues>(new fileStreamCues(std::string()));
  cueRegistry& reg = Tables();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(name);
  if (it != reg.names.end()) {
    if (std::shared_ptr<fileStreamCues> existing = it->second.lock()) return existing;
  }
  std::shared_ptr<fileStreamCues> made(new fileStreamCues(name));
  reg.names[name] = made;
  return made;
}

std::shared_ptr<fileStreamCues> fileStreamCues::find(const std::string& name) {
  if (name.empty()) return nullptr;
  cueRegistry& reg = Tables();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(name);
  return it == reg.names.end() ? nullptr : it->second.lock();
}

void fileStreamCues::Changed() {
  changes.fetch_add(1, std::memory_order_acq_rel);
}

bool fileStreamCues::open(const std::string& file) {
  if (file.empty()) return false;
  {
    header h(file);
    if (!h.handle || h.handle.channels() <= 0 || h.handle.samplerate() <= 0) return false;
  }
  const std::lock_guard<std::mutex> lock(mutex);
  openFile = file;
  return true;
}

std::string fileStreamCues::file() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return openFile;
}

bool fileStreamCues::preload(Int number, const std::string& name, Flt startMs, Flt endMs, Flt speed,
                             bool normalized, bool reverse) {
  const std::string file = name.empty() ? this->file() : name;
  if (number < FIRST_CUE || number > LAST_CUE || file.empty()) return false;
  if (!std::isfinite(startMs) || !std::isfinite(endMs) || !std::isfinite(speed) || speed <= 0.f)
    return false;

  // The header, outside the lock: it may take a while.
  Long frames = 0;
  Int rate = 0;
  {
    header h(file);
    if (!h.handle || h.handle.channels() <= 0 || h.handle.samplerate() <= 0) return false;
    frames = static_cast<Long>(h.handle.frames());
    rate = h.handle.samplerate();
  }
  Long first = 0;
  Long last = frames;
  if (normalized) {
    first = static_cast<Long>(std::floor(static_cast<Dbl>(startMs) * static_cast<Dbl>(frames)));
    if (endMs > 0.f)
      last = static_cast<Long>(std::floor(static_cast<Dbl>(endMs) * static_cast<Dbl>(frames)));
  } else {
    first = MsToFrames(startMs, rate);
    if (endMs > 0.f) last = MsToFrames(endMs, rate);
  }
  first = ClampFrames(first, 0, frames);
  last = ClampFrames(last, 0, frames);
  if (last <= first) return false;

  cue c;
  c.number = number;
  c.file = file;
  c.startMs = static_cast<Flt>(static_cast<Dbl>(first) * 1000.0 / static_cast<Dbl>(rate));
  c.endMs = static_cast<Flt>(static_cast<Dbl>(last) * 1000.0 / static_cast<Dbl>(rate));
  c.speed = speed;
  c.startFrame = first;
  c.endFrame = last;
  c.reverse = reverse;

  const std::lock_guard<std::mutex> lock(mutex);
  auto it = std::lower_bound(table.begin(), table.end(), number,
                             [](const cue& a, Int n) { return a.number < n; });
  if (it != table.end() && it->number == number) {
    *it = std::move(c);
  } else {
    table.insert(it, std::move(c));
  }
  Changed();
  return true;
}

bool fileStreamCues::remove(Int number) {
  const std::lock_guard<std::mutex> lock(mutex);
  auto it = std::lower_bound(table.begin(), table.end(), number,
                             [](const cue& a, Int n) { return a.number < n; });
  if (it == table.end() || it->number != number) return false;
  table.erase(it);
  Changed();
  return true;
}

UInt fileStreamCues::close(const std::string& file) {
  const std::lock_guard<std::mutex> lock(mutex);
  if (openFile == file) openFile.clear();
  const std::size_t before = table.size();
  table.erase(
      std::remove_if(table.begin(), table.end(), [&](const cue& c) { return c.file == file; }),
      table.end());
  const auto removed = static_cast<UInt>(before - table.size());
  if (removed > 0) Changed();
  return removed;
}

UInt fileStreamCues::clear() {
  const std::lock_guard<std::mutex> lock(mutex);
  const auto removed = static_cast<UInt>(table.size());
  table.clear();
  if (removed > 0) Changed();
  return removed;
}

std::vector<fileStreamCues::cue> fileStreamCues::cues() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return table;
}

UInt fileStreamCues::count() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return static_cast<UInt>(table.size());
}

// ─── the streams ──────────────────────────────────────────────────────────────

// One cue's stream: the engine's streaming sound file, positioned at the
// cue's start. Opened and freed off the audio thread; once published, the
// fields below `scratch` are the audio thread's.
struct fileStreamPlayer::stream {
  std::unique_ptr<YSE::INTERNAL::soundFile> file;
  std::string path;
  Int number = 0;
  Long first = 0; // the cue's span, in the file's frames
  Long last = 0;
  Flt speed = 1.f;
  bool reverse = false; // primed from its end, for playing backward
  Int fileRate = 0;
  UInt fileChannels = 0;
  // read() writes one buffer per file channel: STANDARD_BUFFERSIZE each.
  std::vector<buffer> scratch;

  // The audio thread's. The region the stream plays now (a seek changes the
  // open file's), and what read() keeps.
  Long regionFirst = 0;
  Long regionLast = 0;
  Flt pos = 0.f;
  YSE::SOUND_STATUS intent = YSE::SS_STOPPED;
  Flt volume = 0.f;

  // Open `path` and fill its first buffer from `startFrame`, or with
  // `reverse` the buffer before `endFrame` (issue #1288); null when it cannot
  // be opened or the span holds nothing. Blocks on the disk.
  static std::shared_ptr<stream> Open(const std::string& path, Int number, Long startFrame,
                                      Long endFrame, Flt speed, bool reverse) {
    auto s = std::make_shared<stream>();
    s->file = std::make_unique<YSE::INTERNAL::soundFile>(path);
    if (!s->file->loadStream()) return nullptr;
    // A negative speed plays it backward.
    s->file->reversible(true);
    const Int channels = s->file->channels();
    const Int rate = s->file->sampleRate();
    const auto length = static_cast<Long>(s->file->length());
    if (channels <= 0 || rate <= 0) return nullptr;
    const Long first = ClampFrames(startFrame, 0, length);
    const Long last = endFrame <= 0 ? length : ClampFrames(endFrame, 0, length);
    if (last <= first) return nullptr;
    s->path = path;
    s->number = number;
    s->first = first;
    s->last = last;
    s->regionFirst = first;
    s->regionLast = last;
    s->speed = speed;
    s->reverse = reverse;
    s->fileRate = rate;
    s->fileChannels = static_cast<UInt>(channels);
    s->scratch.reserve(s->fileChannels);
    for (UInt c = 0; c < s->fileChannels; ++c)
      s->scratch.emplace_back(YSE::STANDARD_BUFFERSIZE);
    s->file->region(first, last);
    // Filled as a loop: a cue shorter than a block loops from its first
    // block. Render() ends a cue that does not loop at its end itself.
    // Backward the reader starts below the empty front buffer at the end.
    s->file->prime(reverse ? last : first, true, reverse);
    s->pos = reverse ? -1.f : 0.f;
    return s;
  }

  bool Same(const fileStreamCues::cue& c) const {
    return number == c.number && path == c.file && first == c.startFrame && last == c.endFrame &&
           speed == c.speed && reverse == c.reverse;
  }
};

// What the audio thread plays from: the open file and the cue streams, by
// number, and the table version they were opened for.
struct fileStreamPlayer::streamSet {
  std::shared_ptr<fileStreamCues> list;
  std::uint64_t version = 0;
  std::shared_ptr<stream> open;
  std::vector<std::shared_ptr<stream>> cues; // by number

  bool Holds(const stream* s) const {
    if (open.get() == s) return true;
    for (const std::shared_ptr<stream>& c : cues)
      if (c.get() == s) return true;
    return false;
  }
};

// ─── construction ─────────────────────────────────────────────────────────────

fileStreamPlayer::fileStreamPlayer(UInt outputs)
  : list(fileStreamCues::acquire(std::string())), where(STANDARD_BUFFERSIZE) {
  channels(outputs);
  where = 0.f;
}

fileStreamPlayer::~fileStreamPlayer() {
  delete current; // NOLINT(cppcoreguidelines-owning-memory): the handoff's sets
  delete incoming.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
  delete retired.load(std::memory_order_acquire); // NOLINT(cppcoreguidelines-owning-memory)
}

fileStreamPlayer& fileStreamPlayer::channels(UInt outputs) {
  outputs = std::max<UInt>(1, std::min(outputs, MAX_CHANNELS));
  outs.channels(outputs);
  shifters.clear();
  shifters.reserve(outputs);
  for (UInt c = 0; c < outputs; ++c)
    shifters.push_back(std::make_unique<pitchShifter>(shiftQuality));
  return *this;
}

// ─── the control side ─────────────────────────────────────────────────────────

void fileStreamPlayer::CollectLocked() {
  streamSet* old = retired.exchange(nullptr, std::memory_order_acq_rel);
  delete old; // NOLINT(cppcoreguidelines-owning-memory)
}

void fileStreamPlayer::collect() {
  const std::lock_guard<std::mutex> lock(edit);
  CollectLocked();
}

void fileStreamPlayer::Publish(streamSet* next) {
  CollectLocked();
  // A set the audio thread has not taken yet is replaced: the last one wins.
  streamSet* unread = incoming.exchange(next, std::memory_order_acq_rel);
  delete unread; // NOLINT(cppcoreguidelines-owning-memory)
}

bool fileStreamPlayer::Rebuild() {
  // The version before the cues: a change in between leaves the set stale,
  // so it is synced again.
  const std::uint64_t version = list->version();
  const std::vector<fileStreamCues::cue> defined = list->cues();
  std::vector<std::shared_ptr<stream>> next;
  next.reserve(defined.size());
  bool ok = true;
  for (const fileStreamCues::cue& c : defined) {
    std::shared_ptr<stream> kept;
    for (const std::shared_ptr<stream>& b : built) {
      if (b->Same(c)) {
        kept = b;
        break;
      }
    }
    if (kept == nullptr)
      kept = stream::Open(c.file, c.number, c.startFrame, c.endFrame, c.speed, c.reverse);
    if (kept == nullptr) {
      ok = false;
      continue;
    }
    next.push_back(std::move(kept));
  }
  built = next;

  auto* set = new streamSet(); // NOLINT(cppcoreguidelines-owning-memory): handed over
  set->list = list;
  set->version = version;
  set->open = openStream;
  set->cues = std::move(next);
  Publish(set);
  return ok;
}

bool fileStreamPlayer::open(const std::string& file) {
  if (file.empty()) return false;
  const std::lock_guard<std::mutex> lock(edit);
  std::shared_ptr<stream> opened = stream::Open(file, 1, 0, 0, 1.f, false);
  if (opened == nullptr) return false;
  openStream = std::move(opened);
  // The cues are kept as they were built; only the open file changes.
  Rebuild();
  return true;
}

bool fileStreamPlayer::close(const std::string& file) {
  const std::lock_guard<std::mutex> lock(edit);
  const std::string target = file.empty() && openStream != nullptr ? openStream->path : file;
  if (target.empty()) return false;
  const bool closeOpen = openStream != nullptr && openStream->path == target;
  const bool closedCues = list->close(target) > 0;
  if (!closeOpen && !closedCues) return false;
  if (closeOpen) openStream.reset();
  Rebuild();
  return true;
}

std::string fileStreamPlayer::file() const {
  const std::lock_guard<std::mutex> lock(edit);
  return openStream != nullptr ? openStream->path : std::string();
}

bool fileStreamPlayer::preload(Int number, const std::string& file, Flt startMs, Flt endMs,
                               Flt speed, bool normalized, bool reverse) {
  std::shared_ptr<fileStreamCues> table;
  std::string path = file;
  {
    const std::lock_guard<std::mutex> lock(edit);
    table = list;
    if (path.empty() && openStream != nullptr) path = openStream->path;
  }
  if (path.empty()) return false;
  if (!table->preload(number, path, startMs, endMs, speed, normalized, reverse)) return false;
  return sync();
}

void fileStreamPlayer::clear() {
  std::shared_ptr<fileStreamCues> table = cueList();
  table->clear();
  sync();
}

void fileStreamPlayer::clear(const std::vector<Int>& numbers) {
  std::shared_ptr<fileStreamCues> table = cueList();
  for (Int n : numbers)
    table->remove(n);
  sync();
}

void fileStreamPlayer::cueList(std::shared_ptr<fileStreamCues> table) {
  const std::lock_guard<std::mutex> lock(edit);
  list = table != nullptr ? std::move(table) : fileStreamCues::acquire(std::string());
  built.clear(); // another table's streams are not kept
  Rebuild();
}

std::shared_ptr<fileStreamCues> fileStreamPlayer::cueList() const {
  const std::lock_guard<std::mutex> lock(edit);
  return list;
}

bool fileStreamPlayer::sync() {
  const std::lock_guard<std::mutex> lock(edit);
  return Rebuild();
}

// ─── the audio side ───────────────────────────────────────────────────────────

void fileStreamPlayer::Take() {
  // Only while the retired slot is empty, so the old set has somewhere to go.
  if (retired.load(std::memory_order_acquire) != nullptr) return;
  if (incoming.load(std::memory_order_relaxed) == nullptr) return;
  streamSet* next = incoming.exchange(nullptr, std::memory_order_acq_rel);
  if (next == nullptr) return;
  // A stream the new set does not hold was closed or cleared: it falls
  // silent here, and is freed with the old set.
  if (active != nullptr && !next->Holds(active)) {
    active = nullptr;
    activeCue = 0;
    sequenceLength = 0;
  }
  if (fading != nullptr && !next->Holds(fading)) fading = nullptr;
  retired.store(current, std::memory_order_release);
  current = next;
}

fileStreamPlayer::stream* fileStreamPlayer::Find(Int number) const {
  if (current == nullptr || number <= 0) return nullptr;
  if (number == 1) return current->open.get();
  const auto& cues = current->cues;
  auto it = std::lower_bound(cues.begin(), cues.end(), number,
                             [](const std::shared_ptr<stream>& s, Int n) { return s->number < n; });
  return it != cues.end() && (*it)->number == number ? it->get() : nullptr;
}

bool fileStreamPlayer::Loops(Int number) const {
  return looping || (loopCue != 0 && loopCue == number);
}

// Back to the start of its region, silent: what a stream not playing is.
// At a speed below 0 the start is the region's end, read backward (issue
// #1288).
void fileStreamPlayer::Cut(stream* s) {
  const bool backward = rate < 0.f;
  s->file->seek(backward ? s->regionLast : s->regionFirst, Loops(s->number), backward);
  s->pos = backward ? -1.f : 0.f;
  s->intent = SS_STOPPED;
  s->volume = 0.f;
}

// The stream that played makes way for another: it fades out.
void fileStreamPlayer::Retire(stream* s) {
  if (s->intent == SS_STOPPED) return;
  if (s->intent == SS_PAUSED) {
    Cut(s);
    return;
  }
  if (fading != nullptr) Cut(fading);
  fading = s;
  s->intent = SS_WANTSTOSTOP;
}

void fileStreamPlayer::Start(stream* s, Int number) {
  bool cut = false;
  if (s == active) {
    // The cue that plays, again: from its start once the disk has read it.
    Cut(s);
    cut = true;
  } else {
    if (active != nullptr) Retire(active);
    if (s == fading) {
      fading = nullptr;
      Cut(s);
      cut = true;
    }
  }
  if (number == 1 && (s->regionFirst != s->first || s->regionLast != s->last)) {
    // The whole open file again after a seek.
    s->regionFirst = s->first;
    s->regionLast = s->last;
    s->file->region(s->first, s->last);
    Cut(s);
    cut = true;
  }
  // A stream waits at its start for the direction it last played or was
  // primed in. The other way, it starts from its other end once the disk has
  // read that (issue #1288). A speed of 0 plays neither way.
  if (!cut && rate != 0.f && s->file->streamBackward() != (rate < 0.f)) Cut(s);
  active = s;
  activeCue = number;
  s->intent = SS_WANTSTOPLAY;
}

fileStreamPlayer& fileStreamPlayer::play(Int number) {
  if (number <= 0) return stop();
  request = REQUEST::PLAY;
  requestCue = number;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::play(const Int* numbers, UInt count) {
  if (numbers == nullptr || count == 0) return *this;
  count = std::min(count, MAX_LIST);
  std::copy(numbers, numbers + count, requestList);
  requestListLength = count;
  request = REQUEST::LIST;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::seek(Flt startMs, Flt endMs) {
  request = REQUEST::SEEK;
  requestStart = std::isfinite(startMs) ? startMs : 0.f;
  requestEnd = std::isfinite(endMs) ? endMs : 0.f;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::stop() {
  request = REQUEST::STOP;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::pause() {
  request = REQUEST::PAUSE;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::resume() {
  request = REQUEST::RESUME;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::speed(Flt value) {
  rate = std::isfinite(value) ? value : 0.f;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::pitchShift(Flt ratio) {
  if (std::isfinite(ratio) && ratio > 0.f) shiftRatio = ratio;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::pitchShiftCents(Flt cents) {
  if (std::isfinite(cents)) shiftCents = cents;
  return *this;
}

fileStreamPlayer& fileStreamPlayer::quality(pitchShifter::QUALITY value) {
  if (value >= pitchShifter::BASIC && value <= pitchShifter::BEST) shiftQuality = value;
  return *this;
}

bool fileStreamPlayer::playing() const {
  return active != nullptr && active->intent != SS_STOPPED;
}

bool fileStreamPlayer::paused() const {
  return active != nullptr && (active->intent == SS_PAUSED || active->intent == SS_WANTSTOPAUSE);
}

Int fileStreamPlayer::cue() const {
  return active != nullptr ? activeCue : 0;
}

bool fileStreamPlayer::hasCue(Int number) const {
  return Find(number) != nullptr;
}

bool fileStreamPlayer::stale() const {
  return current != nullptr && current->list != nullptr &&
         current->list->version() != current->version;
}

void fileStreamPlayer::Render(stream* s, UInt length, bool withPosition) {
  if (s->intent == SS_STOPPED || s->intent == SS_PAUSED) return;
  // Frames of the file per sample, at the live engine rate. read() applies
  // the factor the stream was loaded with, so it is taken back out.
  const Dbl engine = YSE::SAMPLERATE > 0 ? static_cast<Dbl>(YSE::SAMPLERATE) : 44100.0;
  const Dbl step =
      static_cast<Dbl>(rate) * static_cast<Dbl>(s->speed) * static_cast<Dbl>(s->fileRate) / engine;
  const Flt loaded = s->file->sampleRateAdjustment();
  const auto speedArg = static_cast<Flt>(loaded > 0.f ? step / static_cast<Dbl>(loaded) : step);
  const Dbl from = s->file->streamPosition(s->pos);

  // Within the capacity reserved when the stream was opened: no allocation.
  for (buffer& b : s->scratch)
    if (b.getLength() != length) b.resize(length);
  const bool loops = Loops(s->number);
  if (!s->file->read(s->scratch, s->pos, length, speedArg, loops, s->intent, s->volume)) {
    return;
  }

  const auto first = static_cast<Dbl>(s->regionFirst);
  const auto last = static_cast<Dbl>(s->regionLast);
  const Dbl span = last - first;
  // The stream's count runs past the region's end forward and below its start
  // backward (issue #1288) as the fills wrap: back into the region.
  auto fold = [&](Dbl frame) {
    if (span <= 0.0 || (frame >= first && frame < last)) return frame;
    Dbl into = std::fmod(frame - first, span);
    if (into < 0.0) into += span;
    return first + into;
  };

  // A loop turned off while the stream still holds audio the disk fill
  // wrapped (up to two stream buffers of it): the cue ends where it passes
  // its end (backward: its start), not once that audio has played. A stream
  // filled without a loop stops there by itself.
  if (!loops && s->intent != SS_STOPPED && step != 0.0) {
    const Dbl now = s->file->streamPosition(s->pos);
    bool ends = false;
    Dbl at = 0.0;
    if (step > 0.0) {
      const Dbl remaining = last - fold(from);
      ends = now - from >= remaining;
      at = std::ceil(remaining / step);
    } else {
      // Sample i reads from - i |step|, inside while that is at or above the
      // start.
      const Dbl remaining = fold(from) - first;
      ends = from - now > remaining;
      at = std::floor(remaining / -step) + 1.0;
    }
    if (ends) {
      const UInt endAt = at <= 0.0 ? 0 : (at >= length ? length : static_cast<UInt>(at));
      for (buffer& b : s->scratch)
        std::fill(b.getPtr() + endAt, b.getPtr() + length, 0.f);
      Cut(s);
    }
  }

  const UInt outputs = outs.channels();
  for (UInt k = 0; k < outputs; ++k) {
    UInt source = k;
    if (source >= s->fileChannels) {
      if (!repeating) continue;
      source = k % s->fileChannels;
    }
    const Flt* in = s->scratch[source].getPtr();
    Flt* out = outs.out(k).getPtr();
    for (UInt i = 0; i < length; ++i)
      out[i] += in[i];
  }

  if (withPosition) {
    // The frame the stream count gives, folded into the region it loops.
    const Dbl toMs = 1000.0 / static_cast<Dbl>(s->fileRate);
    Flt* w = where.getPtr();
    for (UInt i = 0; i < length; ++i)
      w[i] = static_cast<Flt>(fold(from + static_cast<Dbl>(i) * step) * toMs);
  }
}

YSE::DSP::buffer& fileStreamPlayer::operator()(UInt length) {
  Take();
  finished = false;
  length = outs.clear(length);
  if (where.getLength() != length) where.resize(length);
  std::fill(where.getPtr(), where.getPtr() + length, 0.f);

  // The transport call since the last block.
  const REQUEST what = request;
  request = REQUEST::NONE;
  switch (what) {
  case REQUEST::PLAY: {
    stream* s = Find(requestCue);
    if (s != nullptr) {
      sequenceLength = 0;
      Start(s, requestCue);
    }
    break;
  }
  case REQUEST::LIST:
    std::copy(requestList, requestList + requestListLength, sequence);
    sequenceLength = requestListLength;
    sequenceNext = 0;
    while (sequenceNext < sequenceLength) {
      const Int n = sequence[sequenceNext++];
      stream* s = Find(n);
      if (s != nullptr) {
        Start(s, n);
        break;
      }
    }
    break;
  case REQUEST::SEEK: {
    stream* s = Find(1);
    if (s == nullptr) break;
    const Long fileEnd = s->last;
    const Long first = ClampFrames(MsToFrames(requestStart, s->fileRate), 0, fileEnd);
    const Long last =
        requestEnd > 0.f ? ClampFrames(MsToFrames(requestEnd, s->fileRate), 0, fileEnd) : fileEnd;
    if (last <= first) break;
    sequenceLength = 0;
    if (active != nullptr && active != s) Retire(active);
    if (fading == s) fading = nullptr;
    s->regionFirst = first;
    s->regionLast = last;
    s->file->region(first, last);
    Cut(s);
    active = s;
    activeCue = 1;
    s->intent = SS_WANTSTOPLAY;
    break;
  }
  case REQUEST::STOP:
    sequenceLength = 0;
    if (active != nullptr) {
      if (active->intent == SS_PAUSED) {
        Cut(active);
        active = nullptr;
        activeCue = 0;
        finished = true;
      } else if (active->intent != SS_STOPPED) {
        active->intent = SS_WANTSTOSTOP;
      }
    }
    break;
  case REQUEST::PAUSE:
    if (active != nullptr && active->intent != SS_STOPPED && active->intent != SS_PAUSED &&
        active->intent != SS_WANTSTOSTOP)
      active->intent = SS_WANTSTOPAUSE;
    break;
  case REQUEST::RESUME:
    // From the volume a pause left, so a resume mid-fade does not jump.
    if (active != nullptr && (active->intent == SS_PAUSED || active->intent == SS_WANTSTOPAUSE))
      active->intent = SS_PLAYING;
    break;
  default:
    break;
  }

  if (fading != nullptr) {
    Render(fading, length, false);
    if (fading->intent == SS_STOPPED) fading = nullptr;
  }
  if (active != nullptr) {
    const bool stopping = active->intent == SS_WANTSTOSTOP;
    Render(active, length, true);
    if (active->intent == SS_STOPPED) {
      // The end of the cue, or of a stop's fade. A list goes on with its
      // next cue it has a stream for, from the next block.
      active = nullptr;
      activeCue = 0;
      bool next = false;
      while (!stopping && !next && sequenceNext < sequenceLength) {
        const Int n = sequence[sequenceNext++];
        stream* s = Find(n);
        if (s != nullptr) {
          Start(s, n);
          next = true;
        }
      }
      if (!next) {
        sequenceLength = 0;
        finished = true;
      }
    }
  }

  // The pitch shifters, only while one of their settings asks for them.
  const bool shifting = shiftRatio != 1.f || shiftCents != 0.f || stretching;
  Flt ratio = shiftRatio * pitchShifter::centsToRatio(shiftCents);
  if (stretching) {
    // Backward too: the pitch follows the speed's size.
    const Flt v = std::fabs(rate * (active != nullptr ? active->speed : 1.f));
    if (v > 0.f) ratio /= v;
  }
  for (UInt k = 0; k < outs.channels(); ++k) {
    pitchShifter& p = *shifters[k];
    p.quality(shiftQuality).enabled(shifting);
    if (!shifting) continue;
    p.ratio(ratio);
    buffer& shifted = p(outs.out(k));
    std::copy(shifted.getPtr(), shifted.getPtr() + length, outs.out(k).getPtr());
  }
  return outs.out(0);
}

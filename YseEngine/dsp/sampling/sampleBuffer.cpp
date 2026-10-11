/*
  ==============================================================================

    sampleBuffer.cpp
    A shared, named, multichannel sample buffer (issue #1039).

  ==============================================================================
*/

#include "sampleBuffer.hpp"
#include "../../headers/constants.hpp"
#include "../../internal/customFileReader.h"
#include "../../io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sndfile.hh>
#include <unordered_map>
#include <utility>

namespace YSE {
  namespace DSP {

    // The storage a buffer publishes. Never changed once published: a change
    // builds a new one (sampleBuffer::edit), so a reader holding this one
    // reads samples nobody is writing.
    struct sampleBuffer::storage {
      UInt channels = 0;
      UInt frames = 0;
      Flt rate = 0.f; // 0: the engine rate
      std::uint64_t version = 0; // the sampleBuffer::version it was published as
      std::vector<Flt> samples; // channel after channel, `frames` each

      storage() = default;
      storage(UInt channelCount, UInt frameCount, Flt sampleRate)
        : channels(channelCount),
          frames(frameCount),
          rate(sampleRate),
          samples(static_cast<std::size_t>(channelCount) * frameCount, 0.f) {}

      Flt* channel(UInt c) {
        return samples.data() + static_cast<std::size_t>(c) * frames;
      }
      const Flt* channel(UInt c) const {
        return samples.data() + static_cast<std::size_t>(c) * frames;
      }
    };

  } // namespace DSP
} // namespace YSE

using namespace YSE::DSP;

namespace {

  constexpr double PI = 3.14159265358979323846;

  Flt EngineRate() {
    return YSE::SAMPLERATE > 0 ? static_cast<Flt>(YSE::SAMPLERATE) : 44100.f;
  }

  UInt MsToFrames(Flt ms, Flt rate) {
    if (!(ms > 0.f)) return 0;
    const double frames = std::floor(static_cast<double>(ms) * rate * 0.001 + 0.5);
    return frames >= 4294967295.0 ? 4294967295u : static_cast<UInt>(frames);
  }

  // ─── the registry ───────────────────────────────────────────────────────────
  //
  // One lock for three tables. Lock order: a buffer's own mutex may be held
  // while this one is taken (publish marks the buffer pending), never the
  // other way round, and nothing drops a shared_ptr<sampleBuffer> while
  // holding this one (the destructor takes it).
  struct registryState {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<sampleBuffer>> names;
    std::unordered_map<std::uint64_t, std::weak_ptr<sampleBuffer>> all;
    std::vector<std::uint64_t> pending; // buffers with retired storage waiting
    std::uint64_t nextId = 1;
  };

  registryState& Registry() {
    static registryState instance;
    return instance;
  }

  void MarkPending(std::uint64_t id) {
    registryState& reg = Registry();
    const std::lock_guard<std::mutex> lock(reg.mutex);
    if (std::find(reg.pending.begin(), reg.pending.end(), id) == reg.pending.end()) {
      reg.pending.push_back(id);
    }
  }

  // ─── windows ────────────────────────────────────────────────────────────────

  // The zeroth-order modified Bessel function of the first kind, by its
  // series. Converges in a few dozen terms for any beta a window uses.
  double BesselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    const double half = x * 0.5;
    for (int k = 1; k < 64; ++k) {
      term *= (half / k) * (half / k);
      sum += term;
      if (term < sum * 1e-12) break;
    }
    return sum;
  }

  // The window at position x in 0..1 (its first to its last point).
  double Window(SAMPLE_WINDOW shape, double x, double beta) {
    switch (shape) {
    case SAMPLE_WINDOW::TRIANGLE:
      return 1.0 - std::fabs(2.0 * x - 1.0);
    case SAMPLE_WINDOW::HAMMING:
      return 0.54 - 0.46 * std::cos(2.0 * PI * x);
    case SAMPLE_WINDOW::HANNING:
      return 0.5 - 0.5 * std::cos(2.0 * PI * x);
    case SAMPLE_WINDOW::BLACKMAN:
      return 0.42 - 0.5 * std::cos(2.0 * PI * x) + 0.08 * std::cos(4.0 * PI * x);
    case SAMPLE_WINDOW::WELCH: {
      const double t = 2.0 * x - 1.0;
      return 1.0 - t * t;
    }
    case SAMPLE_WINDOW::KAISER: {
      const double t = 2.0 * x - 1.0;
      return BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / BesselI0(beta);
    }
    }
    return 1.0;
  }

  // ─── files ──────────────────────────────────────────────────────────────────

  // An open sound file, through the host's virtual file system when it is
  // active, as fileBuffer::load and soundFile do.
  struct openFile {
    SndfileHandle handle;
    void* ioHandle = nullptr;

    explicit openFile(const char* path) {
      if (YSE::IO().getActive()) {
        long long size = 0;
        if (!YSE::INTERNAL::customFileReader::Open(path, &size, &ioHandle)) return;
        handle = SndfileHandle(YSE::INTERNAL::customFileReader::GetVIO(), ioHandle);
      } else {
        handle = SndfileHandle(path);
      }
    }
    ~openFile() {
      // The handle reads through ioHandle, so it closes first.
      handle = SndfileHandle();
      if (ioHandle != nullptr) YSE::INTERNAL::customFileReader::Close(ioHandle);
    }
    openFile(const openFile&) = delete;
    openFile& operator=(const openFile&) = delete;
  };

  int FormatFor(const char* path, SAMPLE_FILE format) {
    if (format == SAMPLE_FILE::AUTO) {
      std::string ext;
      const char* dot = std::strrchr(path, '.');
      if (dot != nullptr) {
        for (const char* c = dot + 1; *c != '\0'; ++c) {
          ext += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
        }
      }
      if (ext == "aif" || ext == "aiff") {
        format = SAMPLE_FILE::AIFF;
      } else if (ext == "flac") {
        format = SAMPLE_FILE::FLAC;
      } else {
        format = SAMPLE_FILE::WAV;
      }
    }
    switch (format) {
    case SAMPLE_FILE::AIFF:
      return SF_FORMAT_AIFF | SF_FORMAT_FLOAT;
    case SAMPLE_FILE::FLAC:
      return SF_FORMAT_FLAC | SF_FORMAT_PCM_24;
    default:
      return SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    }
  }

  constexpr sf_count_t CHUNK_FRAMES = 8192;

} // namespace

// ─── sampleBuffer ─────────────────────────────────────────────────────────────

sampleBuffer::sampleBuffer(std::string name, std::uint64_t id, UInt channels, UInt frames,
                           Flt sampleRate)
  : bufferName(std::move(name)), bufferId(id) {
  auto first = std::make_shared<storage>(channels, frames, sampleRate < 0.f ? 0.f : sampleRate);
  shapeChannels.store(first->channels, std::memory_order_relaxed);
  shapeFrames.store(first->frames, std::memory_order_relaxed);
  shapeRate.store(first->rate, std::memory_order_relaxed);
  published.store(first.get(), std::memory_order_release);
  owner = std::move(first);
}

std::shared_ptr<sampleBuffer> sampleBuffer::create(UInt channels, UInt frames, Flt sampleRate) {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  // Not make_shared: the constructor is private.
  std::shared_ptr<sampleBuffer> made(
      new sampleBuffer(std::string(), reg.nextId++, channels, frames, sampleRate));
  reg.all[made->bufferId] = made;
  return made;
}

sampleBuffer::~sampleBuffer() {
  // Every reader holds a shared_ptr to this buffer, so none is left: the
  // storages go with the members, no hazard can name them.
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  reg.all.erase(bufferId);
  if (!bufferName.empty()) {
    auto it = reg.names.find(bufferName);
    // Only our own, expired entry: acquire() may already have given the name
    // to a new buffer while this one was on its way out.
    if (it != reg.names.end() && it->second.expired()) reg.names.erase(it);
  }
  auto p = std::find(reg.pending.begin(), reg.pending.end(), bufferId);
  if (p != reg.pending.end()) reg.pending.erase(p);
}

UInt sampleBuffer::channels() const {
  return shapeChannels.load(std::memory_order_relaxed);
}

UInt sampleBuffer::frames() const {
  return shapeFrames.load(std::memory_order_relaxed);
}

Flt sampleBuffer::sampleRate() const {
  const Flt rate = shapeRate.load(std::memory_order_relaxed);
  return rate > 0.f ? rate : EngineRate();
}

Flt sampleBuffer::lengthMs() const {
  return static_cast<Flt>(static_cast<double>(frames()) * 1000.0 / sampleRate());
}

std::uint64_t sampleBuffer::version() const {
  return changes.load(std::memory_order_acquire);
}

std::shared_ptr<const sampleBuffer::storage> sampleBuffer::snapshot() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return owner;
}

void sampleBuffer::publish(std::shared_ptr<storage> next) {
  const std::lock_guard<std::mutex> lock(mutex);
  publishLocked(std::move(next));
}

void sampleBuffer::publishLocked(std::shared_ptr<storage> next) {
  next->version = changes.load(std::memory_order_relaxed) + 1;
  shapeChannels.store(next->channels, std::memory_order_relaxed);
  shapeFrames.store(next->frames, std::memory_order_relaxed);
  shapeRate.store(next->rate, std::memory_order_relaxed);

  std::shared_ptr<const storage> old = std::move(owner);
  owner = std::move(next);
  // seq_cst, to pair with the reader's seq_cst hazard store and re-load in
  // pin(): a reader that validated the old pointer stored its hazard before
  // this store, so collectLocked() below sees it.
  published.store(owner.get(), std::memory_order_seq_cst);
  changes.store(owner->version, std::memory_order_release);

  if (old != nullptr) retired.push_back(std::move(old));
  collectLocked();
  if (!retired.empty()) MarkPending(bufferId);
}

void sampleBuffer::collect() {
  const std::lock_guard<std::mutex> lock(mutex);
  collectLocked();
}

void sampleBuffer::collectLocked() {
  for (auto it = retired.begin(); it != retired.end();) {
    bool held = false;
    for (const auto& h : hazards) {
      if (h->held.load(std::memory_order_seq_cst) == it->get()) {
        held = true;
        break;
      }
    }
    // Dropping our reference frees the storage, unless a control-side
    // snapshot (a write() in progress) still holds it; then that frees it.
    it = held ? it + 1 : retired.erase(it);
  }
}

std::size_t sampleBuffer::retiredCount() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return retired.size();
}

template <class Edit> void sampleBuffer::edit(Edit&& change) {
  const std::lock_guard<std::mutex> lock(mutex);
  auto next = std::make_shared<storage>(*owner);
  change(*next);
  publishLocked(std::move(next));
}

sampleBuffer::hazard* sampleBuffer::attach() {
  const std::lock_guard<std::mutex> lock(mutex);
  hazards.push_back(std::make_unique<hazard>());
  return hazards.back().get();
}

void sampleBuffer::detach(hazard* slot) {
  const std::lock_guard<std::mutex> lock(mutex);
  for (auto it = hazards.begin(); it != hazards.end(); ++it) {
    if (it->get() == slot) {
      hazards.erase(it);
      break;
    }
  }
  collectLocked();
}

// ─── files ───────────────────────────────────────────────────────────────────

bool sampleBuffer::read(const char* path, Flt startMs, Flt durationMs, Int channels) {
  if (path == nullptr || path[0] == '\0') return false;

  openFile file(path);
  if (!file.handle) return false;
  const int fileChannels = file.handle.channels();
  const sf_count_t fileFrames = file.handle.frames();
  const Flt fileRate = static_cast<Flt>(file.handle.samplerate());
  if (fileChannels <= 0 || fileFrames < 0 || !(fileRate > 0.f)) return false;

  sf_count_t start = static_cast<sf_count_t>(MsToFrames(startMs, fileRate));
  if (start > fileFrames) start = fileFrames;
  const sf_count_t available = fileFrames - start;

  // The shape now, for the arguments that keep it. Another thread may change
  // it before we publish; the last change wins, as with any two edits.
  const std::shared_ptr<const storage> current = snapshot();

  UInt targetChannels = 0;
  if (channels > 0) {
    targetChannels = static_cast<UInt>(channels);
  } else if (channels == 0 || current->channels == 0) {
    targetChannels = static_cast<UInt>(fileChannels);
  } else {
    targetChannels = current->channels;
  }

  UInt targetFrames = 0;
  sf_count_t toRead = 0;
  if (durationMs < 0.f) {
    if (available > static_cast<sf_count_t>(0xFFFFFFFFu)) return false;
    targetFrames = static_cast<UInt>(available);
    toRead = available;
  } else {
    targetFrames = current->frames;
    toRead =
        std::min<sf_count_t>({available, static_cast<sf_count_t>(MsToFrames(durationMs, fileRate)),
                              static_cast<sf_count_t>(targetFrames)});
  }

  auto next = std::make_shared<storage>(targetChannels, targetFrames, fileRate);
  if (toRead > 0) {
    if (start > 0 && file.handle.seek(start, SEEK_SET) != start) return false;
    std::vector<float> scratch(static_cast<std::size_t>(CHUNK_FRAMES) * fileChannels);
    sf_count_t done = 0;
    while (done < toRead) {
      const sf_count_t want = std::min(CHUNK_FRAMES, toRead - done);
      const sf_count_t got = file.handle.readf(scratch.data(), want);
      if (got <= 0) break;
      for (int c = 0; c < fileChannels; ++c) {
        Flt* out = next->channel(static_cast<UInt>(c) % targetChannels) + done;
        for (sf_count_t i = 0; i < got; ++i) {
          out[i] += scratch[static_cast<std::size_t>(i) * fileChannels + c];
        }
      }
      done += got;
    }
  }

  publish(std::move(next));
  return true;
}

bool sampleBuffer::replace(const char* path) {
  return read(path, 0.f, -1.f, 0);
}

bool sampleBuffer::write(const char* path, SAMPLE_FILE format) const {
  if (path == nullptr || path[0] == '\0') return false;
  // The custom-IO layer has no write callback (fileBuffer::save refuses too).
  if (YSE::IO().getActive()) return false;

  const std::shared_ptr<const storage> data = snapshot();
  if (data->frames == 0 || data->channels == 0) return false;

  const int rate = static_cast<int>(data->rate > 0.f ? data->rate : EngineRate());
  SndfileHandle handle(path, SFM_WRITE, FormatFor(path, format), static_cast<int>(data->channels),
                       rate);
  if (!handle) return false;

  std::vector<float> scratch(static_cast<std::size_t>(CHUNK_FRAMES) * data->channels);
  sf_count_t done = 0;
  const auto total = static_cast<sf_count_t>(data->frames);
  while (done < total) {
    const sf_count_t count = std::min(CHUNK_FRAMES, total - done);
    for (UInt c = 0; c < data->channels; ++c) {
      const Flt* in = data->channel(c) + done;
      for (sf_count_t i = 0; i < count; ++i) {
        scratch[static_cast<std::size_t>(i) * data->channels + c] = in[i];
      }
    }
    if (handle.writef(scratch.data(), count) != count) return false;
    done += count;
  }
  handle.writeSync();
  return true;
}

// ─── shape ───────────────────────────────────────────────────────────────────

void sampleBuffer::setSize(UInt frameCount, Int channelCount) {
  const std::lock_guard<std::mutex> lock(mutex);
  const UInt chans = channelCount >= 0 ? static_cast<UInt>(channelCount) : owner->channels;
  auto next = std::make_shared<storage>(chans, frameCount, owner->rate);
  const UInt keepChannels = std::min(chans, owner->channels);
  const UInt keepFrames = std::min(frameCount, owner->frames);
  for (UInt c = 0; c < keepChannels; ++c) {
    std::copy_n(owner->channel(c), keepFrames, next->channel(c));
  }
  publishLocked(std::move(next));
}

void sampleBuffer::setSizeMs(Flt ms, Int channelCount) {
  setSize(MsToFrames(ms, sampleRate()), channelCount);
}

void sampleBuffer::setSampleRate(Flt rate) {
  edit([rate](storage& s) { s.rate = rate > 0.f ? rate : 0.f; });
}

bool sampleBuffer::crop(UInt startFrame, UInt endFrame) {
  const std::lock_guard<std::mutex> lock(mutex);
  endFrame = std::min(endFrame, owner->frames);
  if (startFrame >= endFrame) return false;
  auto next = std::make_shared<storage>(owner->channels, endFrame - startFrame, owner->rate);
  for (UInt c = 0; c < owner->channels; ++c) {
    std::copy(owner->channel(c) + startFrame, owner->channel(c) + endFrame, next->channel(c));
  }
  publishLocked(std::move(next));
  return true;
}

bool sampleBuffer::cropMs(Flt startMs, Flt endMs) {
  const Flt rate = sampleRate();
  return crop(MsToFrames(startMs, rate), MsToFrames(endMs, rate));
}

// ─── contents ────────────────────────────────────────────────────────────────

void sampleBuffer::clear() {
  fill(0.f);
}

void sampleBuffer::fill(Flt value) {
  edit([value](storage& s) { std::fill(s.samples.begin(), s.samples.end(), value); });
}

void sampleBuffer::fillSine(Flt cycles, Flt divisor, bool cosine) {
  if (divisor == 0.f) divisor = 1.f;
  edit([cycles, divisor, cosine](storage& s) {
    if (s.frames == 0) return;
    const double step = 2.0 * PI * (static_cast<double>(cycles) / divisor) / s.frames;
    for (UInt c = 0; c < s.channels; ++c) {
      Flt* out = s.channel(c);
      for (UInt i = 0; i < s.frames; ++i) {
        const double phase = step * i;
        out[i] = static_cast<Flt>(cosine ? std::cos(phase) : std::sin(phase));
      }
    }
  });
}

void sampleBuffer::applyWindow(SAMPLE_WINDOW window, bool half, Flt beta) {
  edit([window, half, beta](storage& s) {
    if (s.frames == 0) return;
    const double last = s.frames > 1 ? static_cast<double>(s.frames - 1) : 1.0;
    for (UInt i = 0; i < s.frames; ++i) {
      double x = s.frames > 1 ? i / last : 0.5;
      if (half) x = 0.5 + 0.5 * x;
      const auto w = static_cast<Flt>(Window(window, x, beta));
      for (UInt c = 0; c < s.channels; ++c)
        s.channel(c)[i] *= w;
    }
  });
}

void sampleBuffer::applyGain(Flt gain) {
  edit([gain](storage& s) {
    for (Flt& v : s.samples)
      v *= gain;
  });
}

void sampleBuffer::applyOffset(Flt offset) {
  edit([offset](storage& s) {
    for (Flt& v : s.samples)
      v += offset;
  });
}

void sampleBuffer::applyDeltas() {
  edit([](storage& s) {
    for (UInt c = 0; c < s.channels; ++c) {
      Flt* v = s.channel(c);
      for (UInt i = s.frames; i-- > 1;)
        v[i] -= v[i - 1];
    }
  });
}

void sampleBuffer::normalize(Flt peak) {
  edit([peak](storage& s) {
    Flt largest = 0.f;
    for (Flt v : s.samples)
      largest = std::max(largest, std::fabs(v));
    if (largest <= 0.f) return;
    const Flt gain = peak / largest;
    for (Flt& v : s.samples)
      v *= gain;
  });
}

void sampleBuffer::copyFrom(const sampleBuffer& other) {
  // The source's lock, then ours, never both: two buffers copying from each
  // other cannot deadlock.
  const std::shared_ptr<const storage> source = other.snapshot();
  publish(std::make_shared<storage>(*source));
}

void sampleBuffer::setSamples(UInt channel, UInt startFrame, const Flt* values, UInt count) {
  if (values == nullptr || count == 0) return;
  edit([channel, startFrame, values, count](storage& s) {
    if (channel >= s.channels || startFrame >= s.frames) return;
    std::copy_n(values, std::min(count, s.frames - startFrame), s.channel(channel) + startFrame);
  });
}

UInt sampleBuffer::getSamples(UInt channel, UInt startFrame, Flt* out, UInt count) const {
  if (out == nullptr) return 0;
  const std::shared_ptr<const storage> data = snapshot();
  if (channel >= data->channels || startFrame >= data->frames) return 0;
  const UInt n = std::min(count, data->frames - startFrame);
  std::copy_n(data->channel(channel) + startFrame, n, out);
  return n;
}

// ─── sampleBufferRegistry ─────────────────────────────────────────────────────

std::shared_ptr<sampleBuffer> sampleBufferRegistry::acquire(const std::string& name,
                                                            bool* created) {
  if (created != nullptr) *created = false;
  registryState& reg = Registry();
  std::shared_ptr<sampleBuffer> made;
  {
    const std::lock_guard<std::mutex> lock(reg.mutex);
    auto it = reg.names.find(name);
    if (it != reg.names.end()) {
      if (std::shared_ptr<sampleBuffer> existing = it->second.lock()) return existing;
    }
    made.reset(new sampleBuffer(name, reg.nextId++, 1, 0, 0.f));
    reg.names[name] = made;
    reg.all[made->bufferId] = made;
  }
  if (created != nullptr) *created = true;
  return made;
}

std::shared_ptr<sampleBuffer> sampleBufferRegistry::find(const std::string& name) {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(name);
  return it == reg.names.end() ? nullptr : it->second.lock();
}

std::shared_ptr<sampleBuffer> sampleBufferRegistry::findById(std::uint64_t id) {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.all.find(id);
  return it == reg.all.end() ? nullptr : it->second.lock();
}

std::size_t sampleBufferRegistry::count() {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  std::size_t live = 0;
  for (const auto& entry : reg.names) {
    if (!entry.second.expired()) live++;
  }
  return live;
}

void sampleBufferRegistry::collect() {
  std::vector<std::uint64_t> ids;
  {
    registryState& reg = Registry();
    const std::lock_guard<std::mutex> lock(reg.mutex);
    if (reg.pending.empty()) return;
    ids.swap(reg.pending);
  }
  for (std::uint64_t id : ids) {
    std::shared_ptr<sampleBuffer> buffer = findById(id);
    if (buffer == nullptr) continue;
    buffer->collect();
    // Still held by a reader: try again on the next call.
    if (buffer->retiredCount() > 0) MarkPending(id);
  }
}

// ─── sampleView / sampleBufferReader ──────────────────────────────────────────

sampleView::sampleView(sampleView&& other) noexcept
  : slot(other.slot),
    samples(other.samples),
    numChannels(other.numChannels),
    numFrames(other.numFrames),
    rate(other.rate),
    storageVersion(other.storageVersion) {
  other.slot = nullptr;
}

sampleView::~sampleView() {
  if (slot != nullptr) slot->store(nullptr, std::memory_order_release);
}

Flt sampleView::linear(UInt c, Dbl position) const {
  if (c >= numChannels || !(position >= 0.0)) return 0.f;
  const auto index = static_cast<std::size_t>(position);
  if (index >= numFrames) return 0.f;
  const Flt* s = samples + static_cast<std::size_t>(c) * numFrames;
  const auto frac = static_cast<Flt>(position - static_cast<Dbl>(index));
  const Flt next = index + 1 < numFrames ? s[index + 1] : 0.f;
  return s[index] + (next - s[index]) * frac;
}

sampleBufferReader::sampleBufferReader(std::shared_ptr<sampleBuffer> buffer) {
  bind(std::move(buffer));
}

sampleBufferReader::~sampleBufferReader() {
  bind(nullptr);
}

void sampleBufferReader::bind(std::shared_ptr<sampleBuffer> buffer) {
  if (target != nullptr) target->detach(slot);
  slot = nullptr;
  target = std::move(buffer);
  if (target != nullptr) slot = target->attach();
}

sampleView sampleBufferReader::pin() const {
  sampleView view;
  if (target == nullptr || slot == nullptr) return view;

  // A hazard pointer: announce the storage, then check it is still the
  // published one. If a change slipped in between, announce the new one.
  // Only a change published inside that window makes it go round again, and
  // changes come from control threads, so in practice it runs once.
  const std::atomic<const sampleBuffer::storage*>& source = target->published;
  const sampleBuffer::storage* data = source.load(std::memory_order_acquire);
  for (;;) {
    slot->held.store(data, std::memory_order_seq_cst);
    const sampleBuffer::storage* again = source.load(std::memory_order_seq_cst);
    if (again == data) break;
    data = again;
  }

  view.slot = &slot->held;
  view.samples = data->samples.data();
  view.numChannels = data->channels;
  view.numFrames = data->frames;
  view.rate = data->rate > 0.f ? data->rate : EngineRate();
  view.storageVersion = data->version;
  return view;
}

/*
  ==============================================================================

    fileRecorder.cpp
    Records signals to a sound file on disk (issue #1156).

  ==============================================================================
*/

#include "fileRecorder.hpp"
#include "../../io.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <sndfile.hh>

using namespace YSE::DSP;

namespace {

  // Frames converted and written at a time.
  constexpr UInt CHUNK = 4096;

  UInt EngineRate() {
    return YSE::SAMPLERATE > 0 ? YSE::SAMPLERATE : 44100;
  }

  Long MsToFrames(Flt ms, UInt rate) {
    if (!(ms > 0.f)) return 0;
    return static_cast<Long>(std::llround(static_cast<Dbl>(ms) * rate / 1000.0));
  }

  fileRecorder::FILE_TYPE TypeFor(const std::string& path) {
    using T = fileRecorder::FILE_TYPE;
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    std::string ext;
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
      for (std::size_t i = dot + 1; i < path.size(); ++i)
        ext += static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
    }
    if (ext == "wav" || ext == "wave") return T::WAV;
    if (ext == "flac") return T::FLAC;
    if (ext == "ogg" || ext == "oga") return T::OGG;
    if (ext == "au" || ext == "snd") return T::AU;
    if (ext == "caf") return T::CAF;
    if (ext == "w64") return T::W64;
    if (ext == "raw" || ext == "pcm") return T::RAW;
    return T::AIFF; // Max's default
  }

  bool LittleEndian() {
    const std::uint16_t one = 1;
    unsigned char first = 0;
    std::memcpy(&first, &one, 1);
    return first == 1;
  }

  // The libsndfile format for a file type, sample type and byte order.
  int FormatFor(fileRecorder::FILE_TYPE type, fileRecorder::SAMPLE_TYPE sample, bool swap) {
    using T = fileRecorder::FILE_TYPE;
    using S = fileRecorder::SAMPLE_TYPE;
    int major = SF_FORMAT_AIFF;
    switch (type) {
    case T::WAV:
      major = SF_FORMAT_WAV;
      break;
    case T::FLAC:
      major = SF_FORMAT_FLAC;
      break;
    case T::OGG:
      return SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    case T::AU:
      major = SF_FORMAT_AU;
      break;
    case T::CAF:
      major = SF_FORMAT_CAF;
      break;
    case T::W64:
      major = SF_FORMAT_W64;
      break;
    case T::RAW:
      major = SF_FORMAT_RAW;
      break;
    default:
      break;
    }
    int sub = SF_FORMAT_PCM_16;
    switch (sample) {
    case S::INT8:
      // WAV keeps 8-bit samples unsigned.
      sub =
          (major == SF_FORMAT_WAV || major == SF_FORMAT_W64) ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8;
      break;
    case S::INT24:
      sub = SF_FORMAT_PCM_24;
      break;
    case S::INT32:
      sub = SF_FORMAT_PCM_32;
      break;
    case S::FLOAT32:
      sub = SF_FORMAT_FLOAT;
      break;
    case S::FLOAT64:
      sub = SF_FORMAT_DOUBLE;
      break;
    case S::MULAW:
      sub = SF_FORMAT_ULAW;
      break;
    case S::ALAW:
      sub = SF_FORMAT_ALAW;
      break;
    default:
      break;
    }
    if (major == SF_FORMAT_FLAC && sub != SF_FORMAT_PCM_S8 && sub != SF_FORMAT_PCM_16 &&
        sub != SF_FORMAT_PCM_24)
      sub = SF_FORMAT_PCM_24;
    // A raw file has no header to say its byte order: this machine's, or
    // the other.
    int endian = 0;
    if (major == SF_FORMAT_RAW) {
      endian = SF_ENDIAN_CPU;
      if (swap) endian = LittleEndian() ? SF_ENDIAN_BIG : SF_ENDIAN_LITTLE;
    }
    return major | sub | endian;
  }

  // The bits of an integer sample type, which this class quantizes itself;
  // 0 for the others, which libsndfile converts.
  int BitsOf(int format) {
    switch (format & SF_FORMAT_SUBMASK) {
    case SF_FORMAT_PCM_S8:
    case SF_FORMAT_PCM_U8:
      return 8;
    case SF_FORMAT_PCM_16:
      return 16;
    case SF_FORMAT_PCM_24:
      return 24;
    case SF_FORMAT_PCM_32:
      return 32;
    default:
      return 0;
    }
  }

} // namespace

// ─── the writer's state ──────────────────────────────────────────────────────

struct fileRecorder::writer {
  SndfileHandle handle;
  std::string path;
  int format = 0;
  int channels = 1;
  UInt rate = 0;
  int bits = 0; // integer samples quantized here; 0 for the others
  bool clip = false; // mu-law and a-law: libsndfile indexes a table with the sample
  bool rewritable = false;
  std::uint32_t session = 0; // the open file's; 0 none
  bool recording = false; // its START seen
  Long loopFrames = 0;
  Long position = 0; // the frame the next one goes to
  bool wrapped = false;
  Long frames = 0; // the file's length
  Long lastFrames = 0; // the last file closed
  std::vector<float> floats;
  std::vector<int> ints;
  std::vector<double> error; // per channel, for noise shaping
  std::uint64_t noise = 0x9E3779B97F4A7C15ull;

  // Uniform in [0, 1).
  double Uniform() {
    noise ^= noise << 13;
    noise ^= noise >> 7;
    noise ^= noise << 17;
    return static_cast<double>(noise >> 11) * (1.0 / 9007199254740992.0);
  }

  SndfileHandle Create(UInt atRate) const {
    return SndfileHandle(path.c_str(), SFM_WRITE, format, channels, static_cast<int>(atRate));
  }
};

// ─── control and background threads ──────────────────────────────────────────

fileRecorder::fileRecorder(UInt inputs, UInt ringFrames)
  : disk(new writer()), clock(STANDARD_BUFFERSIZE) {
  resize(inputs, ringFrames);
}

fileRecorder::~fileRecorder() {
  const std::lock_guard<std::mutex> lock(edit);
  WriteLocked();
  if (disk->session != 0) Finish();
}

fileRecorder& fileRecorder::resize(UInt inputs, UInt ringFrames) {
  const std::lock_guard<std::mutex> lock(edit);
  WriteLocked();
  if (disk->session != 0) Finish();
  width = std::max<UInt>(1, std::min(inputs, MAX_CHANNELS));
  capacity = std::max(ringFrames, MIN_RING_FRAMES);
  ring.assign(static_cast<std::size_t>(capacity) * width, 0.f);
  head.store(0, std::memory_order_relaxed);
  tail.store(0, std::memory_order_relaxed);
  markHead.store(0, std::memory_order_relaxed);
  markTail.store(0, std::memory_order_relaxed);
  state = STATE::IDLE;
  stopOwed = false;
  request = REQUEST::NONE;
  return *this;
}

bool fileRecorder::open(const std::string& path, FILE_TYPE type) {
  const std::lock_guard<std::mutex> lock(edit);
  WriteLocked();
  if (disk->session != 0) Finish();
  return OpenLocked(path, type);
}

bool fileRecorder::OpenLocked(const std::string& path, FILE_TYPE type) {
  if (path.empty()) return false;
  // The host's virtual file system reads only (sampleBuffer::write refuses
  // too).
  if (YSE::IO().getActive()) return false;
  if (type == FILE_TYPE::AUTO) type = TypeFor(path);
  writer& d = *disk;
  const UInt wanted = nextChannels.load(std::memory_order_relaxed);
  const int channels = static_cast<int>(wanted > 0 ? wanted : width);
  const int format = FormatFor(type, sampleType(), byteSwap());
  const UInt rate = EngineRate();
  SF_INFO info{};
  info.channels = channels;
  info.samplerate = static_cast<int>(rate);
  info.format = format;
  if (sf_format_check(&info) == 0) return false;

  d.path = path;
  d.format = format;
  d.channels = channels;
  d.rate = rate;
  d.handle = d.Create(rate);
  if (!d.handle || d.handle.error() != SF_ERR_NO_ERROR) {
    d.handle = SndfileHandle();
    return false;
  }
  d.bits = BitsOf(format);
  const int sub = format & SF_FORMAT_SUBMASK;
  d.clip = sub == SF_FORMAT_ULAW || sub == SF_FORMAT_ALAW;
  d.rewritable = type != FILE_TYPE::FLAC && type != FILE_TYPE::OGG;
  d.recording = false;
  d.loopFrames = 0;
  d.position = 0;
  d.wrapped = false;
  d.frames = 0;
  const std::size_t samples = static_cast<std::size_t>(CHUNK) * channels;
  if (d.bits > 0) {
    d.ints.assign(samples, 0);
    d.floats.clear();
  } else {
    d.floats.assign(samples, 0.f);
    d.ints.clear();
  }
  d.error.assign(static_cast<std::size_t>(channels), 0.0);
  if (++sessions == 0) sessions = 1;
  d.session = sessions;
  rewritable.store(d.rewritable, std::memory_order_relaxed);
  armed.store(d.session, std::memory_order_release);
  return true;
}

bool fileRecorder::close() {
  const std::lock_guard<std::mutex> lock(edit);
  WriteLocked();
  if (disk->session == 0) return false;
  Finish();
  return true;
}

std::string fileRecorder::file() const {
  const std::lock_guard<std::mutex> lock(edit);
  return disk->session != 0 ? disk->path : std::string();
}

Long fileRecorder::framesWritten() const {
  const std::lock_guard<std::mutex> lock(edit);
  return disk->session != 0 ? disk->frames : disk->lastFrames;
}

bool fileRecorder::pending() const {
  return head.load(std::memory_order_acquire) != tail.load(std::memory_order_acquire) ||
         markHead.load(std::memory_order_acquire) != markTail.load(std::memory_order_acquire);
}

UInt fileRecorder::write() {
  const std::lock_guard<std::mutex> lock(edit);
  return WriteLocked();
}

UInt fileRecorder::WriteLocked() {
  UInt total = 0;
  for (;;) {
    // The marks first: a mark is published after the frames before it, so
    // the frame count read after it covers them.
    const std::uint32_t markEnd = markHead.load(std::memory_order_acquire);
    const std::uint32_t markAt = markTail.load(std::memory_order_relaxed);
    const std::uint64_t end = head.load(std::memory_order_acquire);
    const std::uint64_t at = tail.load(std::memory_order_relaxed);
    const mark* next = markAt != markEnd ? &marks[markAt % MARKS] : nullptr;
    if (next != nullptr && next->frame <= at) {
      Apply(*next);
      markTail.store(markAt + 1, std::memory_order_release);
      continue;
    }
    const std::uint64_t until = next != nullptr ? next->frame : end;
    if (until <= at) break;
    const auto count = static_cast<UInt>(std::min<std::uint64_t>(until - at, capacity));
    // Frames of no file (a session closed under them) are passed over.
    if (disk->recording) total += Store(at, count);
    tail.store(at + count, std::memory_order_release);
  }
  return total;
}

void fileRecorder::Apply(const mark& m) {
  writer& d = *disk;
  if (m.kind == mark::START) {
    d.recording = m.session == d.session && d.session != 0;
    if (!d.recording) return;
    d.loopFrames = d.rewritable ? m.loopFrames : 0;
    d.position = 0;
    d.wrapped = false;
    // The file takes the engine's rate at the start.
    if (m.rate != d.rate && d.frames == 0 && !Reopen(m.rate)) {
      failed.fetch_add(1, std::memory_order_relaxed);
      Finish();
    }
    return;
  }
  if (m.session == d.session && d.session != 0) Finish();
}

bool fileRecorder::Reopen(UInt rate) {
  writer& d = *disk;
  d.handle = SndfileHandle(); // closed before it is made again
  d.handle = d.Create(rate);
  if (!d.handle || d.handle.error() != SF_ERR_NO_ERROR) return false;
  d.rate = rate;
  return true;
}

UInt fileRecorder::Store(std::uint64_t from, UInt frames) {
  writer& d = *disk;
  const auto channels = static_cast<UInt>(d.channels);
  const UInt common = std::min(width, channels);
  const auto shaping = static_cast<DITHER>(ditherMode.load(std::memory_order_relaxed));
  const bool floor =
      quantizeRule.load(std::memory_order_relaxed) == static_cast<std::uint8_t>(QUANTIZE::FLOOR);
  const double scale = d.bits > 0 ? std::ldexp(1.0, d.bits - 1) : 1.0;
  const double lowest = -scale;
  const double highest = scale - 1.0;
  const int shift = 32 - d.bits;

  UInt done = 0;
  while (done < frames) {
    if (d.loopFrames > 0 && d.position >= d.loopFrames) {
      // Round the circle: the oldest frames are written over.
      if (d.handle.seek(0, SEEK_SET) != 0) {
        failed.fetch_add(1, std::memory_order_relaxed);
        Finish();
        return done;
      }
      d.position = 0;
      d.wrapped = true;
    }
    UInt count = std::min(frames - done, CHUNK);
    if (d.loopFrames > 0)
      count = static_cast<UInt>(std::min<Long>(count, d.loopFrames - d.position));

    for (UInt i = 0; i < count; ++i) {
      const Flt* frame = ring.data() + ((from + done + i) % capacity) * width;
      for (UInt c = 0; c < channels; ++c) {
        const Flt sample = c < common ? frame[c] : 0.f;
        const std::size_t at = static_cast<std::size_t>(i) * channels + c;
        if (d.bits == 0) {
          d.floats[at] = d.clip ? std::min(std::max(sample, -1.f), 1.f) : sample;
          continue;
        }
        double x = static_cast<double>(sample) * scale;
        double v = x;
        if (shaping == DITHER::NOISE_SHAPED) {
          x -= d.error[c];
          v = x + d.Uniform() - d.Uniform();
        } else if (shaping == DITHER::TRIANGULAR) {
          v = x + d.Uniform() - d.Uniform();
        }
        double q = floor ? std::floor(v) : std::floor(v + 0.5);
        q = std::min(std::max(q, lowest), highest);
        if (shaping == DITHER::NOISE_SHAPED) d.error[c] = q - x;
        // libsndfile takes ints at full scale and keeps their top bits.
        d.ints[at] = static_cast<int>(static_cast<std::int64_t>(q) * (std::int64_t{1} << shift));
      }
    }
    const sf_count_t wrote = d.bits > 0 ? d.handle.writef(d.ints.data(), count)
                                        : d.handle.writef(d.floats.data(), count);
    if (wrote != static_cast<sf_count_t>(count)) {
      failed.fetch_add(1, std::memory_order_relaxed);
      Finish();
      return done;
    }
    d.position += count;
    d.frames = std::max(d.frames, d.position);
    done += count;
  }
  return done;
}

void fileRecorder::Finish() {
  writer& d = *disk;
  if (d.wrapped && d.position < d.loopFrames && sorting.load(std::memory_order_relaxed)) Rotate();
  d.handle = SndfileHandle(); // closes the file, writing its header
  d.lastFrames = d.frames;
  std::uint32_t expected = d.session;
  armed.compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
  d.session = 0;
  d.recording = false;
  d.loopFrames = 0;
  d.wrapped = false;
}

void fileRecorder::Rotate() {
  // The loop's oldest frame is at `position`: read the file back and write
  // it again from there.
  writer& d = *disk;
  d.handle = SndfileHandle();
  const bool raw = (d.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_RAW;
  SndfileHandle in =
      raw ? SndfileHandle(d.path.c_str(), SFM_READ, d.format, d.channels, static_cast<int>(d.rate))
          : SndfileHandle(d.path.c_str());
  const Long total = d.loopFrames;
  const std::size_t samples = static_cast<std::size_t>(total) * d.channels;
  const std::size_t split = static_cast<std::size_t>(d.position) * d.channels;
  const int sub = d.format & SF_FORMAT_SUBMASK;
  const bool real = sub == SF_FORMAT_FLOAT || sub == SF_FORMAT_DOUBLE;
  // Integers go through as ints and floats as doubles, so nothing changes
  // on the way.
  std::vector<int> ints;
  std::vector<double> reals;
  if (real) {
    reals.resize(samples);
  } else {
    ints.resize(samples);
  }
  const sf_count_t got = real ? in.readf(reals.data(), total) : in.readf(ints.data(), total);
  in = SndfileHandle();
  if (got != total) {
    failed.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (real) {
    std::rotate(reals.begin(), reals.begin() + static_cast<std::ptrdiff_t>(split), reals.end());
  } else {
    std::rotate(ints.begin(), ints.begin() + static_cast<std::ptrdiff_t>(split), ints.end());
  }
  SndfileHandle sorted = d.Create(d.rate);
  const sf_count_t wrote =
      real ? sorted.writef(reals.data(), total) : sorted.writef(ints.data(), total);
  if (!sorted || wrote != total) failed.fetch_add(1, std::memory_order_relaxed);
}

// ─── the audio thread ────────────────────────────────────────────────────────

fileRecorder& fileRecorder::record(bool on) {
  request = on ? REQUEST::START : REQUEST::STOP;
  requestMs = 0.f;
  requestLoopMs = 0.f;
  return *this;
}

fileRecorder& fileRecorder::recordFor(Flt ms) {
  request = REQUEST::START;
  requestMs = ms > 0.f ? ms : 0.f;
  requestLoopMs = 0.f;
  return *this;
}

fileRecorder& fileRecorder::loop(Flt ms) {
  request = REQUEST::START;
  requestMs = 0.f;
  requestLoopMs = ms > 0.f ? ms : 0.f;
  return *this;
}

bool fileRecorder::PushMark(mark::KIND kind, std::uint32_t which, Long loopFrames, UInt rate) {
  const std::uint32_t at = markHead.load(std::memory_order_relaxed);
  if (at - markTail.load(std::memory_order_acquire) >= MARKS) return false;
  mark& m = marks[at % MARKS];
  m.kind = kind;
  m.session = which;
  m.frame = head.load(std::memory_order_relaxed);
  m.loopFrames = loopFrames;
  m.rate = rate;
  markHead.store(at + 1, std::memory_order_release);
  return true;
}

void fileRecorder::Start(Flt ms, Flt loopMs) {
  const std::uint32_t file = armed.load(std::memory_order_acquire);
  if (file == 0 || file == used || stopOwed) return;
  if (loopMs > 0.f && !rewritable.load(std::memory_order_relaxed)) return;
  const UInt rate = EngineRate();
  const Long loopFrames = loopMs > 0.f ? std::max<Long>(1, MsToFrames(loopMs, rate)) : 0;
  if (!PushMark(mark::START, file, loopFrames, rate)) return;
  used = file;
  session = file;
  state = STATE::RECORDING;
  elapsed = 0;
  lost.store(0, std::memory_order_relaxed);
  remaining = ms > 0.f ? std::max<Long>(1, MsToFrames(ms, rate)) : -1;
  fadeIn = MsToFrames(rampIn, rate);
  fadeOut = MsToFrames(rampOut, rate);
}

void fileRecorder::Stop() {
  state = STATE::IDLE;
  remaining = -1;
  if (!PushMark(mark::STOP, session, 0, 0)) {
    stopOwed = true;
    stopSession = session;
  }
}

buffer& fileRecorder::operator()(buffer& in) {
  buffer* one = &in;
  return (*this)(&one, 1);
}

buffer& fileRecorder::operator()(buffer* const* inputs, UInt count) {
  UInt length = STANDARD_BUFFERSIZE;
  for (UInt c = 0; c < count; ++c)
    length = std::min(length, inputs[c]->getLength());
  // Within the capacity set in the constructor: no allocation.
  if (clock.getLength() != length) clock.resize(length);
  Flt* time = clock.getPtr();
  const UInt rate = EngineRate();
  const double msPerFrame = 1000.0 / rate;

  if (stopOwed && PushMark(mark::STOP, stopSession, 0, 0)) stopOwed = false;

  // The file was closed from the control side, or could not be written.
  if (state == STATE::RECORDING && armed.load(std::memory_order_acquire) != session) Stop();

  const REQUEST asked = request;
  request = REQUEST::NONE;
  if (asked == REQUEST::START && state == STATE::IDLE) {
    Start(requestMs, requestLoopMs);
  } else if (asked == REQUEST::STOP) {
    if (state == STATE::RECORDING) {
      if (fadeOut > 0) {
        // Recording goes on while it fades out.
        if (remaining < 0 || remaining > fadeOut) remaining = fadeOut;
      } else {
        Stop();
      }
    } else {
      // Nothing records: the open file is closed, empty.
      const std::uint32_t file = armed.load(std::memory_order_acquire);
      if (file != 0 && file != used && !stopOwed) {
        used = file;
        session = file;
        Stop();
      }
    }
  }

  UInt recorded = 0;
  if (state == STATE::RECORDING) {
    recorded = length;
    if (remaining >= 0) recorded = static_cast<UInt>(std::min<Long>(recorded, remaining));
    const std::uint64_t at = head.load(std::memory_order_relaxed);
    const std::uint64_t room = capacity - (at - tail.load(std::memory_order_acquire));
    const auto fits = static_cast<UInt>(std::min<std::uint64_t>(recorded, room));
    const UInt given = std::min(count, width);
    for (UInt i = 0; i < fits; ++i) {
      const Long k = elapsed + i;
      Flt gain = 1.f;
      if (k < fadeIn) gain = static_cast<Flt>(static_cast<Dbl>(k) / static_cast<Dbl>(fadeIn));
      if (remaining >= 0 && fadeOut > 0) {
        const Long left = remaining - i; // frames to go, this one included
        if (left <= fadeOut)
          gain *= static_cast<Flt>(static_cast<Dbl>(left - 1) / static_cast<Dbl>(fadeOut));
      }
      Flt* frame = ring.data() + ((at + i) % capacity) * width;
      for (UInt c = 0; c < given; ++c)
        frame[c] = inputs[c]->getPtr()[i] * gain;
      for (UInt c = given; c < width; ++c)
        frame[c] = 0.f;
    }
    head.store(at + fits, std::memory_order_release);
    if (fits < recorded) lost.fetch_add(recorded - fits, std::memory_order_relaxed);
    for (UInt i = 0; i < recorded; ++i)
      time[i] = static_cast<Flt>(static_cast<double>(elapsed + i) * msPerFrame);
    elapsed += recorded;
    if (remaining >= 0) {
      remaining -= recorded;
      if (remaining == 0) Stop();
    }
  }
  // After a recording: its length.
  const auto held = static_cast<Flt>(static_cast<double>(elapsed) * msPerFrame);
  std::fill(time + recorded, time + length, held);
  return clock;
}

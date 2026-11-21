/*
  ==============================================================================

    sampleBufferSet.cpp
    A numbered collection of named sample buffers (issue #1139).

  ==============================================================================
*/

#include "sampleBufferSet.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include "../../headers/constants.hpp"

using namespace YSE::DSP;

namespace {

  // ─── the registry of sets ───────────────────────────────────────────────────
  //
  // Its own lock, apart from sampleBufferRegistry's. Nothing drops a
  // shared_ptr<sampleBufferSet> while holding it (the destructor takes it).
  struct setRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<sampleBufferSet>> names;
    std::unordered_map<std::uint64_t, std::weak_ptr<sampleBufferSet>> all;
    std::uint64_t nextId = 1;
  };

  setRegistry& Sets() {
    static setRegistry instance;
    return instance;
  }

  // Milliseconds at the engine rate as it is now, in whole frames.
  UInt FramesAtEngineRate(Flt ms) {
    if (!(ms > 0.f)) return 0;
    const double rate = YSE::SAMPLERATE > 0 ? static_cast<double>(YSE::SAMPLERATE) : 44100.0;
    const double frames = std::floor(static_cast<double>(ms) * rate * 0.001 + 0.5);
    return frames >= 4294967295.0 ? 4294967295u : static_cast<UInt>(frames);
  }

  char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  }

  bool IsDigit(char c) {
    return c >= '0' && c <= '9';
  }

  // Natural order: a run of digits compares as a number, everything else
  // character by character ignoring case; equal names by their bytes, so the
  // order is total.
  bool NaturalLess(const std::string& a, const std::string& b) {
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() && j < b.size()) {
      if (IsDigit(a[i]) && IsDigit(b[j])) {
        // Skip leading zeros, then the longer run is the larger number.
        std::size_t si = i;
        std::size_t sj = j;
        while (si < a.size() && a[si] == '0')
          si++;
        while (sj < b.size() && b[sj] == '0')
          sj++;
        std::size_t ei = si;
        std::size_t ej = sj;
        while (ei < a.size() && IsDigit(a[ei]))
          ei++;
        while (ej < b.size() && IsDigit(b[ej]))
          ej++;
        if (ei - si != ej - sj) return ei - si < ej - sj;
        const int digits = a.compare(si, ei - si, b, sj, ej - sj);
        if (digits != 0) return digits < 0;
        i = ei;
        j = ej;
        continue;
      }
      const char ca = Lower(a[i]);
      const char cb = Lower(b[j]);
      if (ca != cb) return ca < cb;
      i++;
      j++;
    }
    if (i < a.size() || j < b.size()) return j < b.size();
    return a < b;
  }

  const char* ExtensionFor(SAMPLE_FILE format) {
    switch (format) {
    case SAMPLE_FILE::AIFF:
      return ".aif";
    case SAMPLE_FILE::FLAC:
      return ".flac";
    default:
      return ".wav";
    }
  }

  // The file name without folder and extension.
  std::string ShortNameOf(const std::string& path) {
    if (path.empty()) return std::string();
    return std::filesystem::path(path).stem().string();
  }

} // namespace

// ─── the registry ─────────────────────────────────────────────────────────────

sampleBufferSet::sampleBufferSet(std::string name, std::uint64_t id)
  : setName(std::move(name)), setId(id) {}

sampleBufferSet::~sampleBufferSet() {
  setRegistry& reg = Sets();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  reg.all.erase(setId);
  auto it = reg.names.find(setName);
  // Only our own, expired entry: acquire() may already have given the name to
  // a new set while this one was on its way out.
  if (it != reg.names.end() && it->second.expired()) reg.names.erase(it);
}

std::shared_ptr<sampleBufferSet> sampleBufferSet::acquire(const std::string& name, bool* created) {
  if (created != nullptr) *created = false;
  setRegistry& reg = Sets();
  std::shared_ptr<sampleBufferSet> made;
  {
    const std::lock_guard<std::mutex> lock(reg.mutex);
    std::string key = name;
    const std::uint64_t id = reg.nextId++;
    if (key.empty()) {
      // A name of its own, never one somebody already holds.
      std::uint64_t n = id;
      do {
        key = "polybuffer-" + std::to_string(n++);
        auto taken = reg.names.find(key);
        if (taken == reg.names.end() || taken->second.expired()) break;
      } while (true);
    } else {
      auto it = reg.names.find(key);
      if (it != reg.names.end()) {
        if (std::shared_ptr<sampleBufferSet> existing = it->second.lock()) return existing;
      }
    }
    // Not make_shared: the constructor is private.
    made.reset(new sampleBufferSet(key, id));
    reg.names[key] = made;
    reg.all[id] = made;
  }
  if (created != nullptr) *created = true;
  return made;
}

std::shared_ptr<sampleBufferSet> sampleBufferSet::find(const std::string& name) {
  setRegistry& reg = Sets();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(name);
  return it == reg.names.end() ? nullptr : it->second.lock();
}

std::shared_ptr<sampleBufferSet> sampleBufferSet::findById(std::uint64_t id) {
  setRegistry& reg = Sets();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.all.find(id);
  return it == reg.all.end() ? nullptr : it->second.lock();
}

std::string sampleBufferSet::bufferName(const std::string& setName, std::size_t index) {
  return setName + "." + std::to_string(index);
}

// ─── queries ──────────────────────────────────────────────────────────────────

std::size_t sampleBufferSet::count() const {
  return entryCount.load(std::memory_order_acquire);
}

std::uint64_t sampleBufferSet::version() const {
  return changes.load(std::memory_order_acquire);
}

std::shared_ptr<sampleBuffer> sampleBufferSet::buffer(std::size_t index) const {
  const std::lock_guard<std::mutex> lock(mutex);
  if (index == 0 || index > entries.size()) return nullptr;
  return entries[index - 1];
}

std::vector<std::shared_ptr<sampleBuffer>> sampleBufferSet::buffers() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return entries;
}

std::string sampleBufferSet::bufferName(std::size_t index) const {
  return bufferName(setName, index);
}

std::string sampleBufferSet::shortName(std::size_t index) const {
  const std::shared_ptr<sampleBuffer> b = buffer(index);
  return b == nullptr ? std::string() : ShortNameOf(b->info().filePath);
}

std::size_t sampleBufferSet::memoryBytes() const {
  std::size_t bytes = 0;
  for (const std::shared_ptr<sampleBuffer>& b : buffers()) {
    bytes += static_cast<std::size_t>(b->frames()) * b->channels() * sizeof(Flt);
  }
  return bytes;
}

// ─── changes ──────────────────────────────────────────────────────────────────

std::shared_ptr<sampleBuffer> sampleBufferSet::slot(std::size_t index) const {
  return sampleBufferRegistry::acquire(bufferName(index));
}

void sampleBufferSet::add(std::shared_ptr<sampleBuffer> buffer) {
  const std::lock_guard<std::mutex> lock(mutex);
  entries.push_back(std::move(buffer));
  entryCount.store(entries.size(), std::memory_order_release);
  changes.fetch_add(1, std::memory_order_acq_rel);
}

std::shared_ptr<sampleBuffer> sampleBufferSet::append(const char* path) {
  if (path == nullptr || path[0] == '\0') return nullptr;
  const std::lock_guard<std::mutex> edit(editMutex);
  std::shared_ptr<sampleBuffer> next = slot(count() + 1);
  // A failed read leaves the buffer as it was: an empty one made here goes
  // when `next` does, one held elsewhere keeps what it had.
  if (!next->read(path, 0.f, -1.f, 0)) return nullptr;
  add(next);
  return next;
}

std::shared_ptr<sampleBuffer> sampleBufferSet::appendEmpty(Flt ms, UInt channels) {
  const std::lock_guard<std::mutex> edit(editMutex);
  std::shared_ptr<sampleBuffer> next = slot(count() + 1);
  // Through a copy, which also forgets any file a buffer held elsewhere had.
  const std::shared_ptr<sampleBuffer> empty =
      sampleBuffer::create(channels > 0 ? channels : 1, FramesAtEngineRate(ms), 0.f);
  next->copyFrom(*empty);
  add(next);
  return next;
}

std::size_t sampleBufferSet::readFolder(const char* folder) {
  if (folder == nullptr || folder[0] == '\0') return 0;
  namespace fs = std::filesystem;
  std::vector<std::pair<std::string, std::string>> files; // name, path
  std::error_code ec;
  fs::directory_iterator it(fs::path(folder), ec);
  if (ec) return 0;
  for (const fs::directory_iterator end; it != end; it.increment(ec)) {
    if (ec) break;
    std::error_code typeError;
    if (!it->is_regular_file(typeError) || typeError) continue;
    std::string name = it->path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    files.emplace_back(std::move(name), it->path().string());
  }
  std::sort(
      files.begin(), files.end(),
      [](const std::pair<std::string, std::string>& a,
         const std::pair<std::string, std::string>& b) { return NaturalLess(a.first, b.first); });

  std::size_t added = 0;
  for (const auto& file : files) {
    if (append(file.second.c_str()) != nullptr) added++;
  }
  return added;
}

std::size_t sampleBufferSet::writeToFolder(const char* folder, SAMPLE_FILE format) const {
  if (folder == nullptr || folder[0] == '\0') return 0;
  namespace fs = std::filesystem;
  const fs::path base(folder);
  std::error_code ec;
  fs::create_directories(base, ec);
  if (!fs::is_directory(base, ec)) return 0;

  const std::lock_guard<std::mutex> edit(editMutex);
  const std::vector<std::shared_ptr<sampleBuffer>> list = buffers();
  const char* extension = ExtensionFor(format);
  std::unordered_set<std::string> used;
  std::size_t written = 0;
  for (std::size_t i = 0; i < list.size(); ++i) {
    std::string stem = ShortNameOf(list[i]->info().filePath);
    if (stem.empty() || used.count(stem) != 0) stem = bufferName(i + 1);
    used.insert(stem);
    const std::string path = (base / (stem + extension)).string();
    if (list[i]->write(path.c_str(), format)) written++;
  }
  return written;
}

void sampleBufferSet::clear() {
  const std::lock_guard<std::mutex> edit(editMutex);
  std::vector<std::shared_ptr<sampleBuffer>> old;
  {
    const std::lock_guard<std::mutex> lock(mutex);
    old.swap(entries);
    entryCount.store(0, std::memory_order_release);
    changes.fetch_add(1, std::memory_order_acq_rel);
  }
  const std::shared_ptr<sampleBuffer> empty = sampleBuffer::create(1, 0, 0.f);
  for (const std::shared_ptr<sampleBuffer>& b : old) {
    b->copyFrom(*empty);
  }
}

#include "BufferIO.hpp"
#include "io.hpp"
#include <string>
#include <sstream>
#include <algorithm>
#include <forward_list>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "log.hpp"

#include <sndfile.hh>

// One registered buffer. Shared between the registry and every reader opened on
// it (issue #1223): RemoveBufferByName() drops the registry's reference, but a
// streaming soundFile keeps its reader open until the slow pool destroys it,
// and its refill job reads through that reader on a pool thread. The node
// therefore lives until the last reader closes, and the bytes are detached
// (buffer set to null) under dataMutex, so a removal waits out a read already
// copying and every later read answers 0. None of this runs on the audio
// thread: reads come from the load and refill jobs on the slow pool.
class IOBuffer {
public:
  IOBuffer(const char* ID, char* buffer, int length, bool owned)
    : name(ID), buffer(buffer), length(length), owned(owned) {}

  // Stop handing out the bytes, freeing them if this buffer owns a copy. After
  // this returns no reader is inside a copy from them, so the caller may free
  // caller-owned memory.
  void Detach() {
    const std::lock_guard<std::mutex> lock(dataMutex);
    if (owned) delete[] buffer;
    buffer = nullptr;
  }

  const std::string name;
  // Written only by Detach(), under dataMutex. Readers copy under the same
  // lock; the registry's lookups read it under IOBuffers::listMutex, which is
  // also held whenever Detach() runs on a buffer still in the list.
  char* buffer;
  const int length;
  const bool owned;
  std::mutex dataMutex;
};

struct IOBufferReader {
  explicit IOBufferReader(std::shared_ptr<IOBuffer> buffer)
    : buffer(std::move(buffer)), currentPos(0) {}
  std::shared_ptr<IOBuffer> buffer;
  I64 currentPos;
};

struct IOBuffers {

  bool NameExists(const char* ID) {
    const std::lock_guard<std::mutex> lock(listMutex);
    return FindByNameLocked(ID) != list.end();
  }

  bool Exists(char* buffer) {
    const std::lock_guard<std::mutex> lock(listMutex);
    return FindLocked(buffer) != list.end();
  }

  std::shared_ptr<IOBuffer> FindByName(const char* ID) {
    const std::lock_guard<std::mutex> lock(listMutex);
    auto it = FindByNameLocked(ID);
    return it == list.end() ? nullptr : *it;
  }

  void Clear() {
    const std::lock_guard<std::mutex> lock(listMutex);
    for (const std::shared_ptr<IOBuffer>& b : list) {
      b->Detach();
    }
    list.clear();
  }

  bool Add(const char* ID, char* buffer, int length, bool copy) {
    char* data = buffer;
    if (copy) {
      data = new char[length];
      std::copy(buffer, buffer + length, data);
    }
    const std::lock_guard<std::mutex> lock(listMutex);
    if (FindByNameLocked(ID) != list.end()) {
      if (copy) delete[] data;
      return false;
    }
    list.push_front(std::make_shared<IOBuffer>(ID, data, length, copy));
    return true;
  }

  bool RemoveByName(const char* ID) {
    std::shared_ptr<IOBuffer> b;
    {
      const std::lock_guard<std::mutex> lock(listMutex);
      auto it = FindByNameLocked(ID);
      if (it == list.end()) return false;
      b = *it;
      list.remove(b);
    }
    b->Detach();
    return true;
  }

  bool Remove(char* buffer) {
    std::shared_ptr<IOBuffer> b;
    {
      const std::lock_guard<std::mutex> lock(listMutex);
      auto it = FindLocked(buffer);
      if (it == list.end()) return false;
      b = *it;
      list.remove(b);
    }
    b->Detach();
    return true;
  }

private:
  using List = std::forward_list<std::shared_ptr<IOBuffer>>;

  List::iterator FindByNameLocked(const char* ID) {
    return std::find_if(list.begin(), list.end(),
                        [ID](const std::shared_ptr<IOBuffer>& b) { return b->name == ID; });
  }

  List::iterator FindLocked(char* buffer) {
    return std::find_if(list.begin(), list.end(), [buffer](const std::shared_ptr<IOBuffer>& b) {
      return b->buffer == buffer;
    });
  }

  // Guards the list against registrations on the main thread racing lookups
  // from BufferIO_Open on the slow pool. Never taken on the audio thread.
  std::mutex listMutex;
  List list;
};

IOBuffers* buffers = nullptr;

bool BufferIO_Open(const char* filename, long long* filesize, void** fileHandle) {
  std::shared_ptr<IOBuffer> b = buffers->FindByName(filename);

  if (b == nullptr) return false;

  (*filesize) = b->length;
  (*fileHandle) = new IOBufferReader(std::move(b));

  return true;
}

void BufferIO_Close(void* fileHandle) {
  delete (IOBufferReader*)fileHandle;
}

long long BufferIO_Read(void* destBuffer, long long maxBytesToRead, void* fileHandle) {
  IOBufferReader* reader = (IOBufferReader*)fileHandle;
  // Held across the copy, so RemoveBufferByName() cannot free the bytes while
  // this read is inside them (issue #1223). Slow pool only, never audio.
  const std::lock_guard<std::mutex> lock(reader->buffer->dataMutex);
  char* buffer = reader->buffer->buffer;
  // The buffer was removed while this reader stayed open: nothing to hand out.
  if (buffer == nullptr) return 0;
  const sf_count_t length = reader->buffer->length;
  sf_count_t startpos = reader->currentPos;

  // Nothing left to hand out (issue #825). Asking for bytes at or past the end
  // is an ordinary thing for a VFS caller to do — sndfile does it at EOF, and
  // a seek past the end can put the reader here too. Answer 0 rather than
  // falling through to a copy whose end lies before its start.
  if (startpos >= length) {
    reader->currentPos = startpos;
    return 0;
  }

  sf_count_t endpos = startpos + maxBytesToRead;
  // Clamp to the end of the buffer, not one byte short of it: a read finishing
  // exactly at `length` is legal and used to lose its last byte, which made the
  // final byte of every registered buffer unreachable (issue #825).
  if (endpos > length) {
    endpos = length;
  }

  std::copy(buffer + startpos, buffer + endpos, (char*)destBuffer);

  reader->currentPos = endpos;
  return endpos - startpos;
}

long long BufferIO_Length(void* fileHandle) {
  IOBufferReader* reader = (IOBufferReader*)fileHandle;
  return reader->buffer->length;
}

namespace {

  // Fold `base + offset` into [0, length] without ever evaluating the addition
  // when it would leave that range: `offset` comes straight from the caller, so
  // base + offset can overflow for a hostile or corrupt value. `base` is always
  // one of 0, currentPos or length, all of which are inside [0, length], which
  // is what makes `length - base` and `-base` safe to compute up front.
  I64 clampSeekTarget(I64 base, long long offset, I64 length) {
    if (offset > length - base) return length;
    if (offset < -base) return 0;
    return base + static_cast<I64>(offset);
  }

} // namespace

long long BufferIO_Seek(long long offset, int whence, void* fileHandle) {
  IOBufferReader* reader = (IOBufferReader*)fileHandle;
  const I64 length = reader->buffer->length;

  // A registered buffer is a fixed range of bytes, so the reader is kept inside
  // it by construction: every seek clamps into [0, length] and reports where it
  // actually landed (issue #826). Nothing here refuses a seek — libsndfile
  // seeks relative to the current position while walking chunks, and a chunk
  // header that lies sends it backwards past the start or far past the end;
  // that is a malformed file, not a reason to leave the reader pointing outside
  // the buffer for BufferIO_Read to then copy from. A caller that cares gets
  // its answer anyway: the returned position differs from the one it asked for,
  // which is how sndfile detects a bad jump. The upper end is where an ordinary
  // EOF seek lands, and BufferIO_Read answers 0 there (issue #825); the lower
  // end is the one that used to hand std::copy a pointer in front of the
  // buffer.
  switch (whence) {
  case 0: // SEEK_SET
    reader->currentPos = clampSeekTarget(0, offset, length);
    break;
  case 1: // SEEK_CUR
    reader->currentPos = clampSeekTarget(reader->currentPos, offset, length);
    break;
  case 2: // SEEK_END
    reader->currentPos = clampSeekTarget(length, offset, length);
    break;
  default:
    break; // unknown whence: leave the reader where it is
  }
  return reader->currentPos;
}

long long BufferIO_Tell(void* fileHandle) {
  return ((IOBufferReader*)fileHandle)->currentPos;
}

bool BufferIO_FileExists(const char* filename) {
  return buffers->NameExists(filename);
}

YSE::BufferIO::BufferIO(bool storeCopy) : active(false), storeCopy(storeCopy) {
  if (buffers == nullptr) {
    buffers = new IOBuffers();
  }
}

YSE::BufferIO::~BufferIO() {
  if (buffers != nullptr) {
    buffers->Clear();
    delete buffers;
    buffers = nullptr;
  }
}

void YSE::BufferIO::SetActive(bool value) {
  if (active == value) return;
  if (value) {
    IO().open(BufferIO_Open);
    IO().close(BufferIO_Close);
    IO().read(BufferIO_Read);
    IO().getPosition(BufferIO_Tell);
    IO().fileExists(BufferIO_FileExists);
    IO().length(BufferIO_Length);
    IO().seek(BufferIO_Seek);
  }
  IO().setActive(value);
  active = value;
}

bool YSE::BufferIO::GetActive() {
  return active;
}

bool YSE::BufferIO::BufferNameExists(const char* ID) {
  return buffers->NameExists(ID);
}

bool YSE::BufferIO::BufferExists(char* buffer) {
  return buffers->Exists(buffer);
}

bool YSE::BufferIO::AddBuffer(const char* ID, char* buffer, int length) {
  return buffers->Add(ID, buffer, length, storeCopy);
}

bool YSE::BufferIO::RemoveBufferByName(const char* ID) {
  return buffers->RemoveByName(ID);
}

bool YSE::BufferIO::RemoveBuffer(char* buffer) {
  return buffers->Remove(buffer);
}

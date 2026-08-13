#include "BufferIO.hpp"
#include "io.hpp"
#include <string>
#include <sstream>
#include <forward_list>
#include <vector>
#include "log.hpp"

#include <sndfile.hh>

class IOBuffer {
public:
  IOBuffer(const char* ID, char* buffer, int length) : name(ID), buffer(buffer), length(length) {}

  bool operator==(const IOBuffer& b) const {
    return buffer == b.buffer;
  }

  std::string name;
  char* buffer;
  int length;
};

struct IOBufferReader {
  IOBufferReader(IOBuffer* buffer) : buffer(buffer), currentPos(0) {}
  IOBuffer* buffer;
  I64 currentPos;
};

struct IOBuffers {

  bool NameExists(const char* ID) {
    for (IOBuffer& b : list) {
      if (b.name.compare(ID) == 0) {
        return true;
      }
    }
    return false;
  }

  bool Exists(char* buffer) {
    for (IOBuffer& b : list) {
      if (b.buffer == buffer) {
        return true;
      }
    }
    return false;
  }

  IOBuffer* FindByName(const char* ID) {
    for (IOBuffer& b : list) {
      if (b.name.compare(ID) == 0) {
        return &b;
      }
    }
    return nullptr;
  }

  IOBuffer* Find(char* buffer) {
    for (IOBuffer& b : list) {
      if (b.buffer == buffer) {
        return &b;
      }
    }
    return nullptr;
  }

  void Clear(bool deep) {
    if (deep) {
      for (IOBuffer& b : list) {
        delete[] b.buffer;
      }
    }
    list.clear();
  }

  void Add(const char* ID, char* buffer, int length, bool copy) {
    list.emplace_front(ID, buffer, length);
    if (copy) {
      IOBuffer& b = list.front();
      b.buffer = new char[length];
      std::copy(buffer, buffer + length, b.buffer);
    }
  }

  void Delete(IOBuffer* buffer) {
    list.remove(*buffer);
  }

  std::forward_list<IOBuffer> list;
};

IOBuffers* buffers = nullptr;

bool BufferIO_Open(const char* filename, long long* filesize, void** fileHandle) {
  IOBuffer* b = buffers->FindByName(filename);

  if (b == nullptr) return false;

  (*filesize) = b->length;
  (*fileHandle) = new IOBufferReader(b);

  return true;
}

void BufferIO_Close(void* fileHandle) {
  delete (IOBufferReader*)fileHandle;
}

long long BufferIO_Read(void* destBuffer, long long maxBytesToRead, void* fileHandle) {
  IOBufferReader* reader = (IOBufferReader*)fileHandle;
  char* buffer = reader->buffer->buffer;
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
    buffers->Clear(storeCopy);
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
  if (buffers->NameExists(ID)) return false;

  buffers->Add(ID, buffer, length, storeCopy);
  return true;
}

bool YSE::BufferIO::RemoveBufferByName(const char* ID) {
  IOBuffer* b = buffers->FindByName(ID);
  if (b != nullptr) {
    if (storeCopy) delete[] b->buffer;
    buffers->Delete(b);
    return true;
  }
  return false;
}

bool YSE::BufferIO::RemoveBuffer(char* buffer) {
  IOBuffer* b = buffers->Find(buffer);
  if (b != nullptr) {
    if (storeCopy) delete[] b->buffer;
    buffers->Delete(b);
    return true;
  }
  return false;
}

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
  IOBuffer(const char * ID, char * buffer, int length)
    : name(ID), buffer(buffer), length(length)
  {}

  bool operator==(const IOBuffer & b) const {
    return buffer == b.buffer;
  }

  std::string name;
  char * buffer;
  int length;
};

struct IOBufferReader {
  IOBufferReader(IOBuffer * buffer) : buffer(buffer), currentPos(0) {}
  IOBuffer * buffer;
  I64 currentPos;
};

struct IOBuffers {
  
  bool NameExists(const char * ID) {
    for (IOBuffer & b : list) {
      if (b.name.compare(ID) == 0) {
        return true;
      }
    }
    return false;
  }

  bool Exists(char * buffer) {
    for (IOBuffer & b : list) {
      if (b.buffer == buffer) {
        return true;
      }
    }
    return false;
  }

  IOBuffer * FindByName(const char * ID) {
    for (IOBuffer & b : list) {
      if (b.name.compare(ID) == 0) {
        return &b;
      }
    }
    return nullptr;
  }

  IOBuffer * Find(char * buffer) {
    for (IOBuffer & b : list) {
      if (b.buffer == buffer) {
        return &b;
      }
    }
    return nullptr;
  }

  void Clear(bool deep) {
    if (deep) {
      for (IOBuffer & b : list) {
        delete[] b.buffer;
      }
    }
    list.clear();
  }

  void Add(const char * ID, char * buffer, int length, bool copy) {
    list.emplace_front(ID, buffer, length);
    if (copy) {
      IOBuffer & b = list.front();
      b.buffer = new char[length];
      std::copy(buffer, buffer + length, b.buffer);
    }
  }

  void Delete(IOBuffer * buffer) {
    list.remove(*buffer);
  }

  std::forward_list<IOBuffer> list;
};

IOBuffers * buffers = nullptr;

bool BufferIO_Open(const char * filename, long long * filesize, void ** fileHandle) {
  IOBuffer * b = buffers->FindByName(filename);

  if (b == nullptr) return false;

  (*filesize) = b->length;
  (*fileHandle) = new IOBufferReader(b);

  return true;
}

void BufferIO_Close(void * fileHandle) {
  delete (IOBufferReader*)fileHandle;
}

long long BufferIO_Read(void * destBuffer, long long maxBytesToRead, void * fileHandle) {
  IOBufferReader * reader = (IOBufferReader*)fileHandle;
  char * buffer = reader->buffer->buffer;
  sf_count_t startpos = reader->currentPos;
  sf_count_t endpos = startpos + maxBytesToRead;
  if (endpos >= reader->buffer->length) {
    endpos = reader->buffer->length - 1;
  }

  std::copy(buffer + startpos, buffer + endpos, (char*)destBuffer);

  reader->currentPos = endpos;
  return endpos - startpos;
}

long long BufferIO_Length(void * fileHandle) {
  IOBufferReader * reader = (IOBufferReader*)fileHandle;
  return reader->buffer->length;
}

long long BufferIO_Seek(long long offset, int whence, void * fileHandle) {
  IOBufferReader * reader = (IOBufferReader*)fileHandle;
  switch (whence) {
    case 0: reader->currentPos = offset; break;
    case 1: reader->currentPos += offset; break;
    case 2: reader->currentPos = reader->buffer->length + offset; break;
  }
  return reader->currentPos;
}

long long BufferIO_Tell(void * fileHandle) {
  return ((IOBufferReader*)fileHandle)->currentPos;
}

bool BufferIO_FileExists(const char * filename) {
  return buffers->NameExists(filename);
}

YSE::BufferIO::BufferIO(bool storeCopy) 
  : active(false), storeCopy(storeCopy)
{
  if (buffers == nullptr) {
    buffers = new IOBuffers();
  }
}

YSE::BufferIO::~BufferIO() {
  if (buffers != nullptr) {
    buffers->Clear(storeCopy);
    delete buffers;
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

bool YSE::BufferIO::BufferNameExists(const char * ID) {
  return buffers->NameExists(ID);
}

bool YSE::BufferIO::BufferExists(char * buffer) {
  return buffers->Exists(buffer);
}

bool YSE::BufferIO::AddBuffer(const char * ID, char * buffer, int length) {
  if (buffers->NameExists(ID)) return false;

  buffers->Add(ID, buffer, length, storeCopy);
  return true;
}

bool YSE::BufferIO::RemoveBufferByName(const char * ID) {
  IOBuffer * b = buffers->FindByName(ID);
  if (b != nullptr) {
    if (storeCopy) delete[] b->buffer;
    buffers->Delete(b);
    return true;
  }
  return false;
}

bool YSE::BufferIO::RemoveBuffer(char * buffer) {
  IOBuffer * b = buffers->Find(buffer);
  if (b != nullptr) {
    if (storeCopy) delete[] b->buffer;
    buffers->Delete(b);
    return true;
  }
  return false;
}



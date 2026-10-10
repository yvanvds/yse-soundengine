/*
  ==============================================================================

    customFileReader.h
    Created: 23 Apr 2014 5:22:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CUSTOMFILEREADER_H_INCLUDED
#define CUSTOMFILEREADER_H_INCLUDED
#include "../classes.hpp"
#include "../headers/types.hpp"

#include <sndfile.hh>

namespace YSE {
  namespace INTERNAL {
    class customFileReader {
    public:
      customFileReader();
      ~customFileReader();

      // Race-free readers of the host's custom-IO callbacks: each call takes
      // one acquire-load of the published snapshot (no locks, no allocation),
      // so loader threads may call these while the control thread activates
      // or deactivates the custom IO layer (#837). All of them report
      // failure (false / -1 / an all-null VIO table) while inactive.
      static bool Open(const char* filename, long long* filesize, void** fileHandle);
      static void Close(void* fileHandle);
      static bool FileExists(const char* filename);
      static long long Read(void* destBuffer, long long maxBytesToRead, void* fileHandle);

      static void UpdateVIO();
      static void ResetVIO();
      static SF_VIRTUAL_IO& GetVIO();
    };

    // Staging slots, written by the control thread (YSE::io setters) and
    // snapshotted by customFileReader::UpdateVIO() when the layer activates.
    // Never read these from loader threads — that is the #837 data race; go
    // through the customFileReader accessors above instead.
    namespace CALLBACK {
      extern bool (*openPtr)(const char* filename, long long* filesize, void** fileHandle);
      extern void (*closePtr)(void* fileHandle);
      extern long long (*readPtr)(void* destBuffer, long long maxBytesToRead, void* fileHandle);
      extern long long (*getPosPtr)(void* fileHandle);
      extern bool (*fileExists)(const char* filename);
      extern long long (*lengthPtr)(void* fileHandle);
      extern long long (*seekPtr)(long long offset, int whence, void* fileHandle);
    } // namespace CALLBACK

  } // namespace INTERNAL
} // namespace YSE

#endif // CUSTOMFILEREADER_H_INCLUDED

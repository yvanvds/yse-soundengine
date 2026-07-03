/*
  ==============================================================================

    soundLoader.h
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDMANAGER_H_INCLUDED
#define SOUNDMANAGER_H_INCLUDED

#include <forward_list>
#include <mutex>
#include <vector>
#include <ctime>
#include "sound.hpp"
#include "soundMessage.h"
#include "soundInterface.hpp"
#include "soundImplementation.h"
#include "../classes.hpp"
#include "../internal/threadPool.h"
#include "../internal/managerJobs.hpp"
#include "../utils/lfQueue.hpp"

// global object for file loading
// used in system.cpp, soundimpl.cpp and soundfile.cpp

namespace YSE {
  namespace SOUND {

    /**
      The soundmanager is responsible for the management of all soundfiles and
      sound implementations.
    */
    class managerObject {
    public:
      using ImplementationType = implementationObject;

      managerObject();
      ~managerObject() noexcept;

      implementationObject* addImplementation(sound* head);

      /** Add a new soundfile to the system and return a pointer to it. If the file already
          exists, it will not be loaded anew but a pointer to the existing file will be
          returned. With non-streaming audio it would only consume extra memory if loaded
          multiple times. Of course this is not meant to be used for streaming audio.

          @param file   A reference to the soundfile to add to the system

          @return       A pointer to the soundFile object for this file
      */
      // INTERNAL::soundFile * addFile(const File & file);

      // an alternative version of addFile for custom filesystems set with IO()
      INTERNAL::soundFile* addFile(const std::string& fileName);

      // an alternative version to add memory buffers
      INTERNAL::soundFile* addFile(YSE::DSP::buffer* buffer);
      INTERNAL::soundFile* addFile(MULTICHANNELBUFFER* buffer);

      void setup(implementationObject* impl);

      /** Run the soundManager update. This function is responsable for most of the
          action on sound implementations and sound files.
      */
      void update();

      Bool empty();

      /** Sets the maximum amount of sounds to be processed. The soundmanager
          will try to find the sounds that are most relevant and virtualize
          the rest.

          @param value    The desired number of sounds
      */
      // void setMaxSounds(Int value);

      /** Get the maximum amount of sounds to be processed.
       */
      // Int getMaxSounds();

      /** Retrieve a reader for a file format. This function is used by soundFile
          objects.

          @param file   The file to retrieve a reader for.
      */
      // TODO: replace this
      // AudioFormatReader * getReader(const File & f);
      // AudioFormatReader * getReader(juce::InputStream * source);

      /** Hints the sounds manager that it should check for implementations that are
          no longer in use. This is called by the implementations themselves, when they
          find out they're no longer needed.
      */

      // Bool inRange(Flt dist);

    private:
      INTERNAL::managerSetupJob<managerObject> mgrSetup;
      INTERNAL::managerDeleteJob<managerObject> mgrDelete;

      // update() helpers — split out to keep the audio-thread tick function's
      // cyclomatic complexity manageable (cpp:S3776).
      void drainInbox();
      void scrubToLoadAndScheduleSetup();
      void promoteReadyImpls();
      void syncAndReleaseInUse();

      // Garbage-collect unused soundFiles. Runs on the slow pool via mgrFileGC —
      // NEVER on the audio thread: erasing a soundFile runs ~soundFile (sf_close +
      // delete[]) which must stay off the callback, and the erase races addFile
      // on the main thread. Owns `soundFiles` structure together with addFile
      // under `soundFilesMutex` (same pattern as the impl managers). (issue #186)
      void garbageCollectFiles();

      // Slow-pool job wrapper that drives garbageCollectFiles().
      class fileGCJob : public INTERNAL::threadPoolJob {
      public:
        managerObject* owner = nullptr;
        void run() override {
          owner->garbageCollectFiles();
        }
      };
      fileGCJob mgrFileGC;

      // Audio-thread-only: accumulates Time().delta() so update() hands the GC to
      // the slow pool at most about once a second instead of every callback.
      Flt fileGCTimer = 0.f;
      // Slow-pool-only (GC job): previous std::clock() sample, used to measure the
      // elapsed time fed to soundFile::inUse() for the idle timer.
      std::clock_t lastGCClock = 0;

      /** the lastGain buffer of each sound is needed to provide smooth changes
      in volume for each channel. When the number of output channels is changed
      this buffer has to change accordingly. This is done by this function.
      */
      void adjustLastGainBuffer();

      // a forward list containing all sound files. Structure is touched by the
      // main thread (addFile) and the slow-pool GC job (garbageCollectFiles);
      // the audio thread never iterates or erases it. Guarded by soundFilesMutex.
      std::forward_list<INTERNAL::soundFile> soundFiles;

      // Guards `soundFiles` between the main thread (addFile) and the slow-pool
      // GC job. Never taken by the audio thread. (issue #186)
      std::mutex soundFilesMutex;

      // a format Manager to assist in reading audio files
      // It is used by soundFiles, but put here because we only need one for all files.
      // TODO: replace this
      // juce::AudioFormatManager formatManager;

      // the maximum distance before turning virtual
      // This value is calculated on every update
      // aFlt maxDistance;

      // Once an object is ready for use, a pointer is placed in this container. The manager will
      // update and sync all these objects during the dsp callback function
      std::forward_list<implementationObject*> inUse;

      // Lock-free SPSC inbox: main thread pushes here from setup(); audio thread
      // drains it into `toLoad` at the top of update(). This is the only
      // main→audio handoff for new impls — no shared list, no lock.
      lfQueue<implementationObject*> toLoadInbox;

      // Audio-thread-owned working list of impls awaiting OBJECT_READY. The
      // audio thread drains the inbox into it, iterates it to detect readiness,
      // and remove_ifs ready/released/deleted impls out of it. No other thread
      // touches this list.
      std::forward_list<implementationObject*> toLoad;

      // Canonical list of all implementationObjects for this subsystem. Touched
      // by the main thread (emplace_front in addImplementation) and the
      // slow-pool worker (setupJob iterates, deleteJob remove_ifs). The audio
      // thread only reads per-impl atomic objectStatus via pointers it already
      // holds — it never iterates this list. Synchronisation between main and
      // slow-pool is `implementationsMutex`.
      std::forward_list<implementationObject> implementations;

      // Guards `implementations` between the main thread and the slow-pool
      // worker. Never taken by the audio thread.
      std::mutex implementationsMutex;

      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      friend class INTERNAL::managerSetupJob<managerObject>;
      friend class INTERNAL::managerDeleteJob<managerObject>;
    };

    managerObject& Manager();

  } // namespace SOUND
} // namespace YSE

#endif // SOUNDLOADER_H_INCLUDED

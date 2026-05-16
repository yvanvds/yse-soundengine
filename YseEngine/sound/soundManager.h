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
#include "sound.hpp"
#include "soundMessage.h"
#include "soundInterface.hpp"
#include "soundImplementation.h"
#include "../classes.hpp"
#include "../internal/threadPool.h"
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
      
      ////////////////////////////////////////
      // setup threadpool job
      ////////////////////////////////////////

      /** A job to add to the lowpriority threadpool when there are implementationObjects
      that need to be setup.
      */
      class setupJob : public INTERNAL::threadPoolJob {
      public:

        /** The job will be initialized with a name for debug purposes and a pointer
        to the managerObject it is supposed to work with. The managerObject takes
        care of adding this job to a low priority threadpool every time it sees
        that there are implementationObjects that need to be set up.
        */
        setupJob(managerObject * obj)
          : obj(obj) {

        }

        /** This function is called from the threadpool and does the intented work
        (Which is called the setup function of the objects that need to be loaded).

        The slow-pool worker iterates the canonical `implementations` list under
        `implementationsMutex` (shared only with the main thread). For each impl
        whose status is still `OBJECT_CREATED`, a CAS claims it (→
        `OBJECT_SETTING_UP`) and the pointer is snapshotted. The mutex is then
        released *before* calling `setup()` (which may do file I/O), so the main
        thread is not blocked on disk.
        */
        virtual void run() {
          std::vector<implementationObject*> pending;
          {
            std::scoped_lock lk(obj->implementationsMutex);
            for (auto & impl : obj->implementations) {
              if (impl.tryClaimForSetup()) {
                pending.push_back(&impl);
              }
            }
          }
          for (auto * p : pending) p->setup();
        }

      private:
        managerObject * obj;
      };

      ////////////////////////////////////////
      // delete threadpool job
      ////////////////////////////////////////
      /** A job to add to the lowpriority threadpool when there are implementationObjects
      to be deleted
      */
      class deleteJob : public INTERNAL::threadPoolJob {
      public:



        /** The job will be initialized with a name for debug purposes and a pointer
        to the managerObject it is supposed to work with. The managerObject takes
        care of adding this job to a low priority threadpool every time it sees
        that there are implementationObjects that need to be deleted.
        */
        deleteJob(managerObject * obj)
          : obj(obj) {

        }

        virtual void run() {
          std::scoped_lock lk(obj->implementationsMutex);
          obj->implementations.remove_if(implementationObject::canBeDeleted);
        }

      private:
        managerObject * obj;
      };

      ////////////////////////////////////////
      // managerObject
      ////////////////////////////////////////

      managerObject();
      ~managerObject() noexcept;

      implementationObject * addImplementation(sound * head);

      /** Add a new soundfile to the system and return a pointer to it. If the file already
          exists, it will not be loaded anew but a pointer to the existing file will be
          returned. With non-streaming audio it would only consume extra memory if loaded
          multiple times. Of course this is not meant to be used for streaming audio.

          @param file   A reference to the soundfile to add to the system

          @return       A pointer to the soundFile object for this file
      */
      //INTERNAL::soundFile * addFile(const File & file);

      // an alternative version of addFile for custom filesystems set with IO()
      INTERNAL::soundFile * addFile(const std::string & fileName);

      // an alternative version to add memory buffers
      INTERNAL::soundFile * addFile(YSE::DSP::buffer * buffer);
      INTERNAL::soundFile * addFile(MULTICHANNELBUFFER * buffer);

      void setup(implementationObject * impl);

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
      //void setMaxSounds(Int value);	
      
      /** Get the maximum amount of sounds to be processed.
      */
      //Int getMaxSounds();

      /** Retrieve a reader for a file format. This function is used by soundFile
          objects.
      
          @param file   The file to retrieve a reader for.
      */
	  // TODO: replace this
      //AudioFormatReader * getReader(const File & f);
      //AudioFormatReader * getReader(juce::InputStream * source);
      
      
      /** Hints the sounds manager that it should check for implementations that are 
          no longer in use. This is called by the implementations themselves, when they
          find out they're no longer needed.
      */

      //Bool inRange(Flt dist);

      

    private:
      setupJob mgrSetup;
      deleteJob mgrDelete;

      // update() helpers — split out to keep the audio-thread tick function's
      // cyclomatic complexity manageable (cpp:S3776).
      void drainInbox();
      void scrubToLoadAndScheduleSetup();
      void promoteReadyImpls();
      void syncAndReleaseInUse();

      /** the lastGain buffer of each sound is needed to provide smooth changes
      in volume for each channel. When the number of output channels is changed
      this buffer has to change accordingly. This is done by this function.
      */
      void adjustLastGainBuffer();

      // a forward list containing all sound files
      std::forward_list<INTERNAL::soundFile> soundFiles;

      // a format Manager to assist in reading audio files
      // It is used by soundFiles, but put here because we only need one for all files.
      // TODO: replace this
	  //juce::AudioFormatManager formatManager;

      // the maximum distance before turning virtual
      // This value is calculated on every update
      //aFlt maxDistance;

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

      friend class setupJob;
      friend class deleteJob;

    };

    managerObject & Manager();

  }
}




#endif  // SOUNDLOADER_H_INCLUDED

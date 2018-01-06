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
#include "sound.hpp"
#include "soundMessage.h"
#include "soundInterface.hpp"
#include "soundImplementation.h"
#include "../classes.hpp"
#include "../internal/threadPool.h"


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
        (Which is called the setup function of the objects that need to be loaded)
        */
        virtual void run() {
          for (auto i = obj->toLoad.begin(); i != obj->toLoad.end(); ++i) {
            i->load()->setup();
          }
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
          obj->implementations.remove_if(implementationObject::canBeDeleted);
        }

      private:
        managerObject * obj;
      };

      ////////////////////////////////////////
      // managerObject
      ////////////////////////////////////////

      managerObject();
      ~managerObject();

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

      // this queue is used by the setupJob. It is accessed from a low
      // priority thread to setup, but also from the dsp thread to check if an
      // object is ready. This is why every pointer has to be atomic. (It's not
      // a lot of overhead because objects are only in this container while being
      // created. Unless you create a huge amount of sounds at the same time the size
      // of this list will be small. And if you DO create a huge amount of sounds
      // at the same time you should be expecting some latency while they all get loaded
      // anyway.)
      std::forward_list<std::atomic<implementationObject*>> toLoad;

      // this is the list of all implementationObjects for this subSystem, whether they are ready, 
      // need to be setup or are about to be deleted. This list is not accessed from the 
      // audio callback thread, although elements of it might be accessed through the above pointer lists.
      std::forward_list<implementationObject> implementations;

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

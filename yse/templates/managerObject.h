/*
  ==============================================================================

    manager.h
    Created: 9 Mar 2014 8:22:14pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MANAGEROBJECT_H_INCLUDED
#define MANAGEROBJECT_H_INCLUDED

#include "../headers/types.hpp"
#include "../internal/global.h"
#include "../headers/enums.hpp"
#include <forward_list>

namespace YSE {
  namespace TEMPLATE {
    
    /**
      Base class for all manager classes that are part of a subSystem. It also includes
      2 ThreadPoolJobs to handle setting up and deleting objects without locking.
    */
    template <typename SUBSYSTEM>
    class managerTemplate {
   
    public:
      //typedef typename SUBSYSTEM::managerObject derrivedManager;
      typedef typename SUBSYSTEM::interfaceObject derrivedInterface;
      typedef typename SUBSYSTEM::implementationObject derrivedImplementation;

      /** A job to add to the lowpriority threadpool when there are implementationObjects
      that need to be setup.
      */
      class setupJob : public ThreadPoolJob {
      public:

        /** The job will be initialized with a name for debug purposes and a pointer
            to the managerObject it is supposed to work with. The managerObject takes
            care of adding this job to a low priority threadpool every time it sees
            that there are implementationObjects that need to be set up.
        */
        setupJob(const String & name, managerTemplate<SUBSYSTEM> * obj)
        : ThreadPoolJob(name), obj(obj) {
          
        }

        /** This function is called from the threadpool and does the intented work
            (Which is called the setup function of the objects that need to be loaded)
        */
        JobStatus runJob() {
          for (auto i = obj->toLoad.begin(); i != obj->toLoad.end(); ++i) {
            i->load()->setup();
          }
          return jobHasFinished;
        }

      private:
        managerTemplate<SUBSYSTEM> * obj;
      };

      /** A job to add to the lowpriority threadpool when there are implementationObjects
      to be deleted
      */
      class deleteJob : public ThreadPoolJob {
      public:

        /** The job will be initialized with a name for debug purposes and a pointer
        to the managerObject it is supposed to work with. The managerObject takes
        care of adding this job to a low priority threadpool every time it sees
        that there are implementationObjects that need to be deleted.
        */
        deleteJob(const String & name, managerTemplate<SUBSYSTEM> * obj)
          : ThreadPoolJob(name), obj(obj) {

        }

        JobStatus runJob() {
          obj->implementations.remove_if(derrivedImplementation::canBeDeleted);
          return jobHasFinished;
        }

      private:
        managerTemplate<SUBSYSTEM> * obj;
      };

      /** The constructor is responsible for constructing the two threadpooljobs every manager needs
      */
      managerTemplate(const String & name) : mgrSetup(name, this), mgrDelete(name, this) {
      }
      

      ~managerTemplate() {
        // wait for jobs to finish
        INTERNAL::Global().waitForSlowJob(&mgrSetup);
        INTERNAL::Global().waitForSlowJob(&mgrDelete);

        // remove all objects that are still in memory
        toLoad.clear();
        inUse.clear();
        implementations.clear();
      }

      /**
        The create function will be called by the global system after constructing
        a manager. If a manager does not need to do anything right after constructing it
        you should just include an empty function.
      */
      virtual void create() = 0;

      /** Request a new implementationObject. This should be called from every derived interfaceObject.
          It creates an implementationObject that will be linked to the interfaceObject.

      &param head   The interface to connect the new implementation to

      @return       A pointer to a new object implementation
      */
      derrivedImplementation * addImplementation(derrivedInterface * head) {
        implementations.emplace_front(head);
        return &implementations.front();
      }

      /** This function instructs the manager to put the implementation
      in a list of objects to load. It is called from an derived interfaceObject
      create function, preferably at the end when all custom work is done.

      &param impl   The implementation to setup
      */
      void setup(derrivedImplementation * impl) {
        impl->setStatus(OBJECT_CREATED);
        toLoad.emplace_front(impl);
      }

      /** Run the manager update. This function is responsable for most of the
      action on implementations.
      */
      virtual void update() {
        ///////////////////////////////////////////
        // check if there are implementations that need setup
        ///////////////////////////////////////////
        if (!toLoad.empty() && !INTERNAL::Global().containsSlowJob(&mgrSetup)) {
          // removing cannot be done in a separate thread because we are iterating over this
          // list a during this update fuction
          toLoad.remove_if(derrivedImplementation::canBeRemovedFromLoading);
          INTERNAL::Global().addSlowJob(&mgrSetup);
        }

        if (runDelete && !INTERNAL::Global().containsSlowJob(&mgrDelete)) {
          INTERNAL::Global().addSlowJob(&mgrDelete);
        }
        runDelete = false;

        ///////////////////////////////////////////
        // check if loaded implementations are ready
        ///////////////////////////////////////////
        {
          for (auto i = toLoad.begin(); i != toLoad.end(); i++) {
            if (i->load()->readyCheck()) {
              derrivedImplementation * ptr = i->load();
              // place ptr in active sound list
              inUse.emplace_front(ptr);
              // add the sound to the channel that is supposed to use
              //ptr->parent->connect(ptr);
              ptr->doThisWhenReady();
            }
          }
        }

        ///////////////////////////////////////////
        // sync and update implementations
        ///////////////////////////////////////////
        {
          auto previous = inUse.before_begin();
          for (auto i = inUse.begin(); i != inUse.end();) {
            (*i)->sync();
            if ((*i)->getStatus() == OBJECT_RELEASE) {
              derrivedImplementation * ptr = (*i);
              i = inUse.erase_after(previous);
              ptr->setStatus(OBJECT_DELETE);
              runDelete = true;
              continue;
            }
            // update
            (*i)->update();
            previous = i;
            ++i;
          }
        }
      }

      /** Returns true if no implementations exist
      */
      Bool empty() {
        return implementations.empty();
      }

    protected:
      // Once an object is ready for use, a pointer is placed in this container. The manager will
      // update and sync all these objects during the dsp callback function
      std::forward_list<derrivedImplementation*> inUse;

    private:
      setupJob mgrSetup;
      deleteJob mgrDelete;

      // this queue is used by the setupJob. It is accessed from a low
      // priority thread to setup, but also from the dsp thread to check if an
      // object is ready. This is why every pointer has to be atomic. (It's not
      // a lot of overhead because objects are only in this container while being
      // created. Unless you create a huge amount of sounds at the same time the size
      // of this list will be small. And if you DO create a huge amount of sounds
      // at the same time you should be expecting some latency while they all get loaded
      // anyway.)
      std::forward_list<std::atomic<derrivedImplementation*>> toLoad;

      // this is the list of all implementationObjects for this subSystem, whether they are ready, 
      // need to be setup or are about to be deleted. This list is not accessed from the 
      // audio callback thread, although elements of it might be accessed through the above pointer lists.
      std::forward_list<derrivedImplementation> implementations;
      
      // This flag will be set when the audio thread detects that one or more objects
      // should be released. It will result in the deleteJob to be added to the threadpool.
      aBool runDelete;

      friend class setupJob;
      friend class deleteJob;
    };


  }
}



#endif  // MANAGER_H_INCLUDED

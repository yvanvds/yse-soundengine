/*
  ==============================================================================

    manager.h
    Created: 9 Mar 2014 8:22:14pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MANAGER_H_INCLUDED
#define MANAGER_H_INCLUDED

#include "JuceHeader.h"
#include "global.h"
#include <forward_list>
#include <atomic>

namespace YSE {
  namespace INTERNAL {
    /**
      Base class for all manager classes
    */
    template <class IMPLEMENTATION, class INTERFACE>
    class manager {
    public:

      /** A job to add to the lowpriority threadpool when there are implementations
      that need to be setup
      */
      class setupJob : public ThreadPoolJob {
      public:

        setupJob(const String & name, manager<IMPLEMENTATION, INTERFACE> * n) 
        : ThreadPoolJob(name), m(m) {
          
        }

        JobStatus runJob() {
          for (auto i = m->toLoad.begin(); i != m->toLoad.end(); ++i) {
            i->load()->setup();
          }
          return jobHasFinished;
        }

      private:
        manager<IMPLEMENTATION, INTERFACE> * m;
      };

      /** A job to add to the lowpriority threadpool when there are implementations
      to be deleted
      */
      class deleteJob : public ThreadPoolJob {
      public:

        deleteJob(const String & name, manager<IMPLEMENTATION, INTERFACE> * n)
          : ThreadPoolJob(name), m(m) {

        }

        JobStatus runJob() {
          m->implementations.remove_if(IMPLEMENTATION::canBeDeleted);
          return jobHasFinished;
        }

      private:
        manager<IMPLEMENTATION, INTERFACE> * m;
      };



      manager(const String & name) : mgrSetup(name, this), mgrDelete(name, this) {
      }
      
      ~manager() {
        // wait for jobs to finish
        Global.waitForSlowJob(&mgrSetup);
        Global.waitForSlowJob(&mgrDelete);

        // remove all objects that are still in memory
        toLoad.clear();
        inUse.clear();
        implementations.clear();
      }


      /** Request a new sound implementation.

      &param head   The interface to connect the new implementation to

      @return       A pointer to a new object implementation
      */
      IMPLEMENTATION * addImplementation(INTERFACE * head) {
        implementations.emplace_front(head);
        return &implementations.front();
      }

      /** This function instructs the manager to put the implementation
      in a list of objects to load. It is called from the object interface
      create function.

      &param impl   The implementation to setup
      */
      void setup(IMPLEMENTATION * impl) {
        toLoad.emplace_front(impl);
      }

      /** Run the manager update. This function is responsable for most of the
      action on implementations.
      */
      virtual void update() {
        ///////////////////////////////////////////
        // check if there are implementations that need setup
        ///////////////////////////////////////////
        if (!toLoad.empty() && !Global.containsSlowJob(&mgrSetup)) {
          // removing cannot be done in a separate thread because we are iterating over this
          // list a during this update fuction
          toLoad.remove_if(IMPLEMENTATION::canBeRemovedFromLoading);
          Global.addSlowJob(&mgrSetup);
        }

        if (runDelete && !Global.containsSlowJob(&mgrDelete)) {
          Global.addSlowJob(&mgrDelete);
        }
        runDelete = false;

        ///////////////////////////////////////////
        // check if loaded implementations are ready
        ///////////////////////////////////////////
        {
          for (auto i = toLoad.begin(); i != toLoad.end(); i++) {
            if (i->load()->readyCheck()) {
              IMPLEMENTATION * ptr = i->load();
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
            if ((*i)->objectStatus == OBJECT_RELEASE) {
              IMPLEMENTATION * ptr = (*i);
              i = inUse.erase_after(previous);
              ptr->objectStatus = OBJECT_DELETE;
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

      /** Returns true if no implementations are created
      */
      Bool empty() {
        return implementations.empty();
      }


    private:
      setupJob mgrSetup;
      deleteJob mgrDelete;

      std::forward_list<std::atomic<IMPLEMENTATION*>> toLoad;
      std::forward_list<IMPLEMENTATION*> inUse;
      std::forward_list<IMPLEMENTATION> implementations;

      aBool runDelete;

      friend class setupJob;
      friend class deleteJob;
    };


  }
}



#endif  // MANAGER_H_INCLUDED

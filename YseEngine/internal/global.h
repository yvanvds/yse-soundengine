/*
  ==============================================================================

    global.h
    Created: 27 Jan 2014 10:18:27pm
    Author:  yvan

  ==============================================================================
*/

#ifndef GLOBAL_H_INCLUDED
#define GLOBAL_H_INCLUDED

#include <memory>

#include "../headers/types.hpp"
#include "../classes.hpp"
#include "threadPool.h"

namespace YSE {

  namespace INTERNAL {

    class NamedBus;

    class global {
    public:
      bool isActive() { return active; }

      // True between the end of system::initShared() and the start of
      // system::close(). Within a session, SAMPLERATE is immutable: the device
      // writers in portaudioDeviceManager.cpp / oboeImplementation.cpp assert
      // !isSampleRateLocked() before mutating SAMPLERATE. Lookup tables and
      // other SAMPLERATE-derived caches across the engine rely on this
      // contract.
      bool isSampleRateLocked() { return sampleRateLocked; }

      void addSlowJob(threadPoolJob * job);
      void addFastJob(threadPoolJob * job);

      void flagForUpdate() {
        update++;
      }
      bool needsUpdate() { return update > 0;  }
      void updateDone() { update--; }

      // Global named bus (issue #121). Constructed lazily in init() and
      // destroyed in close() so subscribers, the SPSC queue, and the next
      // handle counter do not persist across an init/close cycle. Calling
      // namedBus() before init() or after close() is a programming error.
      NamedBus& namedBus();

      // Embedded-CPython lifecycle (issue #124). These are no-ops unless the
      // engine was built with YSE_ENABLE_PYTHON; the interpreter and the
      // script thread it owns are file-static in global.cpp so this header
      // carries no Python dependency and no macro-dependent layout. Called by
      // system::init / update / close around the audio-device boundary —
      // start after the device opens, stop before it closes.
      void startScripting();
      void wakeScripting();
      void stopScripting();

      // Scripting C-API bridge (issue #125). The C API (yse_python.cpp) is the
      // only consumer; these stay in INTERNAL so the C ABI surface never leaks
      // a C++ type. All are no-ops (or trivial stores) unless the engine was
      // built with YSE_ENABLE_PYTHON, and they carry no Python dependency.

      // Sink invoked on the main thread, once per Error result drained from the
      // script runtime's outbound queue in drainScriptResults(). The string is
      // the formatted traceback and is valid only for the duration of the call.
      using ScriptErrorSink = void (*)(const char* traceback, void* userdata);

      // Install (or clear, with nullptr) the error sink. Stored with the
      // project's atomic-swap callback convention so the C API can install or
      // replace it concurrently with a dispatch already in flight.
      void setScriptErrorSink(ScriptErrorSink sink, void* userdata);

      // Enqueue UTF-8 source for asynchronous evaluation on the script thread.
      // The runtime takes a copy; the caller may free its buffer on return.
      void pushScript(std::string source);

      // Drain every completed result; for each Error, invoke the installed sink
      // with its traceback. Called from system::update() on the main thread.
      void drainScriptResults();

      global();
      ~global();


    private:
      void init();
      void close();

      threadPool slowThreads;
      threadPool fastThreads;

      std::unique_ptr<NamedBus> bus;

      aInt update;
      aBool active; // set true after System().init(), false at System().close()
      aBool sampleRateLocked;


      friend class YSE::system; // system needs access to the init and close method
    };

    global & Global();
  }
}



#endif  // GLOBAL_H_INCLUDED

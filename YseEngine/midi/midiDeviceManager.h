#pragma once
#include "headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include "../dependencies/rtmidi/include/RtMidi.h"
#include "../midi/midiMessage.hpp"
#include <map>
#include <memory>
#include <mutex>

namespace YSE {
  namespace MIDI {

    /**
     *  @brief The process's one handle on RtMidi's view of the machine — how
     *         many ports there are, what they are called, and one open
     *         ``RtMidiOut`` per port a patch sends to.
     *
     *  ### Every entry point takes ``mutex_``, and none of them is RT-safe
     *
     *  This was reachable from more than one thread long before it was
     *  synchronised (issue #757): ``system.cpp``'s public queries run on the
     *  control thread, ``inHub::OpenPortIfNeeded`` asks for the input count
     *  while holding the hub's own lock, ``.midiout`` opens its port from a
     *  message handler, and ``.midiinfo``'s rescan enumerates on the background
     *  pool. Meanwhile ``isPrepared`` lazily constructs the ``RtMidiIn`` /
     *  ``RtMidiOut`` members and flips ``initialized``, and ``getMidiOutPort``
     *  mutates ``midiOutPorts`` — both plain data races under concurrent calls.
     *
     *  A mutex is the right answer rather than a compromise: **every** entry
     *  point here already allocates, returns a ``std::string``, or opens a
     *  device, so none of them was ever safe on the audio callback and locking
     *  costs a caller that may lock nothing at all. The rule this class asks of
     *  its callers is therefore unchanged and is the whole contract: *the audio
     *  thread must never call any of this*. ``.midiinfo`` keeps that rule by
     *  enumerating on the background pool (``midiPortScanner``); ``.midiout``
     *  does not yet, and its lazy open is tracked separately.
     *
     *  ### Lock ordering
     *
     *  ``inHub`` takes its own ``mtx`` and then calls in here, so the order is
     *  always ``inHub::mtx`` → ``deviceManager::mutex_``. Nothing in this class
     *  calls back into ``inHub``, so the reverse edge does not exist and the
     *  pair cannot deadlock.
     */
    class deviceManager {
    public:
      deviceManager();
      ~deviceManager();

      deviceManager(const deviceManager&) = delete;
      deviceManager& operator=(const deviceManager&) = delete;
      deviceManager(deviceManager&&) = delete;
      deviceManager& operator=(deviceManager&&) = delete;

      unsigned int getNumMidiInDevices();
      unsigned int getNumMidiOutDevices();

      const std::string getMidiInDeviceName(unsigned int ID);
      const std::string getMidiOutDeviceName(unsigned int ID);

      RtMidiOut* getMidiOutPort(unsigned int ID);

    private:
      // Caller holds `mutex_`: this constructs the RtMidi backends on first
      // use and flips `initialized`, which is exactly what the lock is for.
      bool isPrepared();

      // Serialises every public entry point. Recursive locking is impossible:
      // nothing below the lock re-enters this class from the outside.
      std::mutex mutex_;

      std::unique_ptr<RtMidiIn> midiIn;
      std::unique_ptr<RtMidiOut> midiOut;
      bool initialized = false;

      std::map<unsigned int, std::unique_ptr<RtMidiOut>> midiOutPorts;
    };

    deviceManager& DeviceManager();

    void GenerateMidiError(const RtMidiError& error);
  } // namespace MIDI
} // namespace YSE

#endif
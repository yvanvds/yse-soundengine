#pragma once
#include "headers/defines.hpp"
// Patcher midi-out object: the only one of the patcher's MIDI senders that
// needs the RtMidi-backed device backend, because it holds an output port
// rather than just formatting bytes. When YSE_ENABLE_MIDI_DEVICE is OFF the
// underlying YSE::midiOut type doesn't exist, so this file is empty. The extra
// `YSE_WINDOWS` this used to carry was lifted with the rest of the family
// (issue #746) — YSE_ENABLE_MIDI_DEVICE is already exactly the set of
// platforms with a port to open, and it is what the input family uses.
//
// ### The port is opened off the dispatching thread (issue #759)
//
// The open stays lazy — a patch must load on a machine whose devices are not
// the ones it was written on — but it no longer happens *in* the list handler.
// That handler runs on whichever thread dispatched the message, and in-patcher
// delivery dispatches on `T_DSP`, so the first list a patch sent used to
// construct an `RtMidiOut`, call `openPort`, insert into a `std::map` and (since
// #757) take `MIDI::deviceManager`'s mutex, all from the audio callback.
//
// The handler now asks `midiPortOpener` instead — one CAS and one lock-free
// push — and the open runs on the background pool. Messages that arrive before
// it lands have no device to go to and are dropped, which is exactly what
// already happened to every message when the open failed. See midiPortOpener.h
// for why the job cannot live in this object.
//
// ### The send is off the dispatching thread too (issue #949)
//
// #759 left the send itself in the handler: `RtMidiOut::sendMessage` is a
// driver call on every backend and can block (and, for SysEx, allocate). The
// handler now pushes the bytes onto `MIDI::outSender`'s immediate lane — one
// lock-free push of a fixed-size slot — and the sender thread performs the
// RtMidi call. A full lane drops the message and counts it in `Dropped`.
#if YSE_ENABLE_MIDI_DEVICE
#include "../pObject.h"
#include "../../midi/device.hpp"
#include "midiPortOpener.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(mMidiOut, YSE::OBJ::M_OUT)
    ~mMidiOut() override;

    mMidiOut(const mMidiOut&) = delete;
    mMidiOut& operator=(const mMidiOut&) = delete;
    mMidiOut(mMidiOut&&) = delete;
    mMidiOut& operator=(mMidiOut&&) = delete;

    _DO_MESSAGES
    _NO_CALCULATE

    _LIST_IN(SetListValue)

    /** @brief Whether the deferred open has landed and been collected — the
     *         attempt, not its success: a port index this machine does not have
     *         settles closed. Diagnostics and tests. */
    bool PortSettled() const {
      return ready.load(std::memory_order_acquire);
    }

    /** @brief Whether a device port is actually open. Diagnostics and tests;
     *         a patch learns the same thing from its instrument making sound. */
    bool PortOpen() const {
      return out.rawPort() != nullptr;
    }

    /** @brief Whether an open is on the background pool or waiting to be
     *         collected. Diagnostics and tests. */
    bool OpenInFlight() const;

    /** @brief Messages dropped because the port was not open yet. Monotonic,
     *         readable from any thread. Counted rather than logged: the
     *         dropping thread may be the audio callback. */
    std::uint64_t Deferred() const {
      return deferred.load(std::memory_order_relaxed);
    }

    /** @brief Messages dropped because the MIDI sender's queue was full
     *         (issue #949). Monotonic, readable from any thread; counted
     *         rather than logged for the same reason as ``Deferred``. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Test seam: behave as if the deferred open had landed on
     *         @p device. Nothing dereferences it while a raw send hook is
     *         installed on ``MIDI::OutSender()``. */
    void UsePortForTest(RtMidiOut* device);

  private:
    // Hand @p length bytes to MIDI::outSender's immediate lane (issue #949).
    // Lock-free and allocation-free on any thread; the RtMidi call happens on
    // the sender thread.
    void Send(const unsigned char* data, std::size_t length);
    void SendControl(unsigned char channel, unsigned char controller, unsigned char value);

    // True once this object owns an opened (or definitively failed) port.
    // Otherwise ask the opener for one and say no — the caller has nothing to
    // send on. Wait-free either way, whichever thread this turns out to be.
    bool EnsurePort();

    // The default matters on its own, and more so now that the index travels to
    // a background thread: `Parameters::Set` returns before touching anything
    // when there are no arguments at all, so a bare `.midiout` never has this
    // written and an uninitialised atomic would hand `openPort` a garbage index.
    // 0 is what the parameter documentation already promises.
    aInt port{0};
    YSE::midiOut out;

    // This object's slot in the process-wide opener (issue #759), taken for the
    // object's whole life. 0 when the table was full, in which case the port
    // never opens and every message is counted as deferred — the same outcome
    // an open that always failed already had.
    midiPortOpener::Handle open{0};

    // The latch. Written only by a handler, so no background thread races it;
    // atomic because handlers run on whichever thread dispatched, and its
    // acquire load is what publishes the port the pool opened.
    std::atomic<bool> ready{false};

    // The port the open produced, cached when the latch is set so a send needs
    // no access to `out` at all. Device-manager-owned, open until process exit
    // — which is what makes it safe to queue for the sender thread.
    std::atomic<RtMidiOut*> sendPort{nullptr};

    std::atomic<std::uint64_t> deferred{0};
    std::atomic<std::uint64_t> dropped{0};
  };
}
}
#endif

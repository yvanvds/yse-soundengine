/*
  ==============================================================================

    midiInHub.cpp
    Created for issue #529 — the patcher's MIDI input plumbing.

  ==============================================================================
*/

#include "midiInHub.h"

#if YSE_ENABLE_MIDI_DEVICE

#include <string>

#include "../implementations/logImplementation.h"
#include "midiDeviceManager.h"

YSE::MIDI::inHub& YSE::MIDI::InHub() {
  static inHub s;
  return s;
}

YSE::MIDI::inHub::~inHub() {
  // Nothing here can throw out: Close() only deletes YSE::midiIn objects, whose
  // destructors already swallow RtMidi's errors into the log.
  Close();
}

bool YSE::MIDI::inHub::SplitHandle(Handle handle, unsigned int& port, unsigned int& index) {
  if (handle == kNoHandle) return false;
  const unsigned int zero = handle - 1;
  port = zero / kMaxSubscriptionsPerPort;
  index = zero % kMaxSubscriptionsPerPort;
  return port < kMaxPorts;
}

void YSE::MIDI::inHub::RawTrampoline(double, const unsigned char* bytes, std::size_t len,
                                     void* user) {
  if (user == nullptr) return;
  auto* ctx = static_cast<portContext*>(user);
  if (ctx->hub == nullptr) return;
  ctx->hub->Deliver(ctx->port, bytes, len);
}

void YSE::MIDI::inHub::OpenPortIfNeeded(unsigned int port) {
  portSlot& slot = ports[port];
  if (slot.device != nullptr) return;

  slot.context.hub = this;
  slot.context.port = port;
  slot.device = std::make_unique<YSE::midiIn>();
  // The raw callback rather than the parsed one: this hub is the *transport*
  // and must not decide what a message means. `.midiin` wants the bytes as they
  // arrived, `.sysexin` (issue #531) wants a stream the parsed nibble split
  // cannot express, and every channel-voice object can read its own fields out
  // of three bytes far more cheaply than a second fan-out would cost.
  slot.device->setRawCallback(&inHub::RawTrampoline, &slot.context);
  // Asked before opening rather than instead of checking the result: RtMidi
  // reports "no MIDI input sources found" by printing to stderr from inside
  // openPort, once per attempt, which on a machine with no hardware turns every
  // subscription into console noise nothing can filter. The count comes from
  // the device manager, which holds one RtMidiIn for the life of the process,
  // so asking is a port-count query rather than another device open.
  if (port < YSE::MIDI::DeviceManager().getNumMidiInDevices()) {
    slot.device->create(port);
  }
  if (!slot.device->isOpen()) {
    // Not fatal, and deliberately not a reason to refuse the subscription: a
    // machine with no MIDI hardware, or a port another client already holds
    // exclusively, leaves an object that receives nothing rather than an object
    // that could not be created. `create()` has already logged RtMidi's own
    // reason where there was one.
    INTERNAL::LogImpl().emit(E_MIDI_WARNING, "MIDI: could not open input port " +
                                                 std::to_string(port) +
                                                 "; patcher objects on it will receive nothing");
  }
}

void YSE::MIDI::inHub::ClosePortIfUnused(unsigned int port) {
  portSlot& slot = ports[port];
  if (slot.subscribers != 0 || slot.device == nullptr) return;
  // Detach before destroying, so the RtMidi thread cannot be inside the
  // trampoline with a context that is about to go. `~midiIn` calls
  // `cancelCallback()`, which is what actually waits the callback out; clearing
  // the pointer first only removes the fan-out.
  slot.device->setRawCallback(nullptr, nullptr);
  slot.device.reset();
}

YSE::MIDI::inHub::Handle YSE::MIDI::inHub::Subscribe(unsigned int port) {
  if (port >= kMaxPorts) {
    INTERNAL::LogImpl().emit(E_MIDI_WARNING, "MIDI: input port " + std::to_string(port) +
                                                 " is past the patcher's port limit (" +
                                                 std::to_string(kMaxPorts) + ")");
    return kNoHandle;
  }

  std::scoped_lock lk(mtx);

  for (unsigned int i = 0; i < kMaxSubscriptionsPerPort; i++) {
    subscription& sub = subscriptions[port][i];
    if (sub.inUse.load(std::memory_order_relaxed)) continue;

    // Claimed but not yet live: the producer skips it while the previous
    // owner's leftovers are cleared out. Draining here rather than in
    // Unsubscribe is what makes the clear-out safe — the producer may still
    // have been mid-push when the last owner let go.
    sub.inUse.store(true, std::memory_order_relaxed);
    inEvent discard;
    while (sub.queue.try_pop(discard)) {}
    sub.dropped.store(0, std::memory_order_relaxed);
    sub.overflowReported.store(false, std::memory_order_relaxed);
    sub.live.store(true, std::memory_order_release);

    ports[port].subscribers++;
    OpenPortIfNeeded(port);
    return MakeHandle(port, i);
  }

  INTERNAL::LogImpl().emit(E_MIDI_WARNING, "MIDI: input port " + std::to_string(port) +
                                               " already has " +
                                               std::to_string(kMaxSubscriptionsPerPort) +
                                               " patcher listeners; refusing another");
  return kNoHandle;
}

void YSE::MIDI::inHub::Unsubscribe(Handle handle) {
  unsigned int port = 0;
  unsigned int index = 0;
  if (!SplitHandle(handle, port, index)) return;

  std::scoped_lock lk(mtx);
  subscription& sub = subscriptions[port][index];
  if (!sub.inUse.load(std::memory_order_relaxed)) return;

  // live first: from here the producer skips this slot, so the next Subscribe
  // is the only thread that will touch the queue again.
  sub.live.store(false, std::memory_order_release);
  sub.inUse.store(false, std::memory_order_relaxed);

  if (ports[port].subscribers > 0) ports[port].subscribers--;
  ClosePortIfUnused(port);
}

bool YSE::MIDI::inHub::TryPop(Handle handle, inEvent& event) {
  unsigned int port = 0;
  unsigned int index = 0;
  if (!SplitHandle(handle, port, index)) return false;
  // No lock and no ownership check: the handle names a queue that exists for
  // the life of the hub, and only its owner ever pops from it.
  return subscriptions[port][index].queue.try_pop(event);
}

void YSE::MIDI::inHub::Deliver(unsigned int port, const unsigned char* bytes, std::size_t len) {
  if (port >= kMaxPorts || bytes == nullptr || len == 0) return;

  for (unsigned int i = 0; i < kMaxSubscriptionsPerPort; i++) {
    subscription& sub = subscriptions[port][i];
    if (!sub.live.load(std::memory_order_acquire)) continue;

    bool lost = false;
    for (std::size_t offset = 0; offset < len; offset += inEvent::kMaxBytes) {
      inEvent e;
      const std::size_t chunk =
          (len - offset) < inEvent::kMaxBytes ? (len - offset) : inEvent::kMaxBytes;
      for (std::size_t b = 0; b < chunk; b++) {
        e.bytes[b] = bytes[offset + b];
      }
      e.len = static_cast<unsigned char>(chunk);
      // try_push, never push: the allocating form would malloc on the RtMidi
      // input thread and grow a queue whose whole point is that it is bounded.
      if (!sub.queue.try_push(e)) {
        sub.dropped.fetch_add(1, std::memory_order_relaxed);
        lost = true;
      }
    }

    if (lost) {
      // Once per episode. This is the RtMidi input thread, not the audio
      // callback, so a log line here is allowed — but a line per lost message
      // under a stall would be a torrent, and the flag below is what keeps the
      // report proportional to the fault rather than to its duration.
      if (!sub.overflowReported.exchange(true, std::memory_order_relaxed)) {
        INTERNAL::LogImpl().emit(E_MIDI_WARNING,
                                 "MIDI: input queue full on port " + std::to_string(port) +
                                     "; incoming messages are being dropped (the patcher is not "
                                     "draining — a stalled audio thread, or an object that stopped "
                                     "rendering)");
      }
    } else {
      sub.overflowReported.store(false, std::memory_order_relaxed);
    }
  }
}

std::uint64_t YSE::MIDI::inHub::Dropped(Handle handle) const {
  unsigned int port = 0;
  unsigned int index = 0;
  if (!SplitHandle(handle, port, index)) return 0;
  return subscriptions[port][index].dropped.load(std::memory_order_relaxed);
}

bool YSE::MIDI::inHub::PortIsOpen(unsigned int port) const {
  if (port >= kMaxPorts) return false;
  std::scoped_lock lk(mtx);
  return ports[port].device != nullptr && ports[port].device->isOpen();
}

void YSE::MIDI::inHub::Close() {
  std::scoped_lock lk(mtx);
  for (unsigned int p = 0; p < kMaxPorts; p++) {
    for (unsigned int i = 0; i < kMaxSubscriptionsPerPort; i++) {
      subscriptions[p][i].live.store(false, std::memory_order_release);
      subscriptions[p][i].inUse.store(false, std::memory_order_relaxed);
    }
    ports[p].subscribers = 0;
    ClosePortIfUnused(p);
  }
}

#endif // YSE_ENABLE_MIDI_DEVICE

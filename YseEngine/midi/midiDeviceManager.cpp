#include "headers/defines.hpp"

#if YSE_ENABLE_MIDI_DEVICE

#include "midiDeviceManager.h"
#include "internalHeaders.h"

#include <mutex>

YSE::MIDI::deviceManager& YSE::MIDI::DeviceManager() {
  static deviceManager d;
  return d;
}

// simple conversion pipe between RtMidi and our log system
void YSE::MIDI::GenerateMidiError(const RtMidiError& error) {
  switch (error.getType()) {
  case RtMidiError::Type::WARNING: {
    INTERNAL::LogImpl().emit(E_MIDI_WARNING, error.getMessage());
    break;
  }
  case RtMidiError::Type::DEBUG_WARNING: {
    INTERNAL::LogImpl().emit(E_MIDI_DEBUG_WARNING, error.getMessage());
    break;
  }
  case RtMidiError::Type::UNSPECIFIED: {
    INTERNAL::LogImpl().emit(E_MIDI_UNSPECIFIED, error.getMessage());
    break;
  }
  case RtMidiError::Type::NO_DEVICES_FOUND: {
    INTERNAL::LogImpl().emit(E_MIDI_NO_DEVICES_FOUND, error.getMessage());
    break;
  }
  case RtMidiError::Type::INVALID_DEVICE: {
    INTERNAL::LogImpl().emit(E_MIDI_INVALID_DEVICE, error.getMessage());
    break;
  }
  case RtMidiError::Type::MEMORY_ERROR: {
    INTERNAL::LogImpl().emit(E_MIDI_MEMORY_ERROR, error.getMessage());
    break;
  }
  case RtMidiError::Type::INVALID_PARAMETER: {
    INTERNAL::LogImpl().emit(E_MIDI_INVALID_PARAMETER, error.getMessage());
    break;
  }
  case RtMidiError::Type::INVALID_USE: {
    INTERNAL::LogImpl().emit(E_MIDI_INVALID_USE, error.getMessage());
    break;
  }
  case RtMidiError::Type::DRIVER_ERROR: {
    INTERNAL::LogImpl().emit(E_MIDI_DRIVER_ERROR, error.getMessage());
    break;
  }
  case RtMidiError::Type::SYSTEM_ERROR: {
    INTERNAL::LogImpl().emit(E_MIDI_SYSTEM_ERROR, error.getMessage());
    break;
  }
  case RtMidiError::Type::THREAD_ERROR: {
    INTERNAL::LogImpl().emit(E_MIDI_THREAD_ERROR, error.getMessage());
    break;
  }
  }
}

YSE::MIDI::deviceManager::deviceManager() = default;

YSE::MIDI::deviceManager::~deviceManager() {
  // No lock: this is a process-lifetime singleton torn down after every thread
  // that could reach it is gone. Taking `mutex_` here would only be able to
  // hide the bug of a live caller during static destruction, not fix it.
  //
  // unique_ptr members handle midiIn/midiOut cleanup automatically;
  // only the explicit closePort() side-effect on map entries needs ordering.
  for (const auto& [id, port] : midiOutPorts) {
    port->closePort();
  }
}

unsigned int YSE::MIDI::deviceManager::getNumMidiInDevices() {
  const std::scoped_lock lock(mutex_);
  if (isPrepared()) {
    return midiIn->getPortCount();
  }
  return 0;
}

unsigned int YSE::MIDI::deviceManager::getNumMidiOutDevices() {
  const std::scoped_lock lock(mutex_);
  if (isPrepared()) {
    return midiOut->getPortCount();
  }
  return 0;
}

// The name getters answer "" when the backend never came up (issue #585). The
// old literal "Invalid Call" was a sentinel only in this file's own head: at the
// C boundary yse_system_midi_in_device_name() copies it out verbatim, so a
// binding enumerating ports on a host without a MIDI backend — headless Linux
// has no ALSA sequencer, so MidiInAlsa::initialize fails — was handed a
// twelve-character "device". An empty name is the answer the rest of the surface
// already gives: getNumMidi*Devices() returns 0 on this same not-prepared path,
// and with a live backend an out-of-range ID yields "" from RtMidi's getPortName.
const std::string YSE::MIDI::deviceManager::getMidiInDeviceName(unsigned int ID) {
  const std::scoped_lock lock(mutex_);
  if (isPrepared()) {
    return midiIn->getPortName(ID);
  }
  return "";
}

const std::string YSE::MIDI::deviceManager::getMidiOutDeviceName(unsigned int ID) {
  const std::scoped_lock lock(mutex_);
  if (isPrepared()) {
    return midiOut->getPortName(ID);
  }
  return "";
}

RtMidiOut* YSE::MIDI::deviceManager::getMidiOutPort(unsigned int ID) {
  const std::scoped_lock lock(mutex_);
  if (auto existing = midiOutPorts.find(ID); existing != midiOutPorts.end()) {
    return existing->second.get();
  }

  try {
    auto port = std::make_unique<RtMidiOut>();
    port->openPort(ID);
    auto [iter, ok] = midiOutPorts.emplace(ID, std::move(port));
    return iter->second.get();
  } catch (RtMidiError& error) {
    MIDI::GenerateMidiError(error);
    return nullptr;
  }
}

bool YSE::MIDI::deviceManager::isPrepared() {
  if (initialized) return true;

  try {
    midiIn = std::make_unique<RtMidiIn>();
  } catch (RtMidiError& error) {
    GenerateMidiError(error);
    RtMidiError::Type type = error.getType();

    // return when this is more than a warning
    if (type != RtMidiError::Type::WARNING && type != RtMidiError::Type::DEBUG_WARNING) {
      midiIn.reset();
      return false;
    }
  }

  try {
    midiOut = std::make_unique<RtMidiOut>();
  } catch (RtMidiError& error) {
    GenerateMidiError(error);
    RtMidiError::Type type = error.getType();

    // return when this is more than a warning
    if (type != RtMidiError::Type::WARNING && type != RtMidiError::Type::DEBUG_WARNING) {
      midiOut.reset();
      return false;
    }
  }

  initialized = true;
  return true;
}

#endif
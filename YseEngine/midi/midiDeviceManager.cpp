#include "midiDeviceManager.h"
#include "internalHeaders.h"

YSE::MIDI::deviceManager& YSE::MIDI::DeviceManager()
{
	static deviceManager d;
	return d;
}

// simple conversion pipe between RtMidi and our log system
void YSE::MIDI::GenerateMidiError(const RtMidiError & error)
{
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

YSE::MIDI::deviceManager::deviceManager()
	: midiIn(nullptr)
	, midiOut(nullptr)
	, initialized(false)
{

}

YSE::MIDI::deviceManager::~deviceManager() {
	if (midiIn != nullptr) delete midiIn;
	if (midiOut != nullptr) delete midiOut;
	for (auto it = midiOutPorts.begin(); it != midiOutPorts.end(); it++) {
		it->second->closePort();
		delete it->second;
	}
}

unsigned int YSE::MIDI::deviceManager::getNumMidiInDevices()
{
	if (isPrepared()) {
		return midiIn->getPortCount();
	}
	return 0;
}

unsigned int YSE::MIDI::deviceManager::getNumMidiOutDevices()
{
	if (isPrepared()) {
		return midiOut->getPortCount();
	}
	return 0;
}

const std::string YSE::MIDI::deviceManager::getMidiInDeviceName(unsigned int ID)
{
	if (isPrepared()) {
		return midiIn->getPortName(ID);
	}
	return "Invalid Call";
}

const std::string YSE::MIDI::deviceManager::getMidiOutDeviceName(unsigned int ID)
{
	if (isPrepared()) {
		return midiOut->getPortName(ID);
	}
	return "Invalid Call";
}

RtMidiOut* YSE::MIDI::deviceManager::getMidiOutPort(unsigned int ID) {
	if (midiOutPorts.count(ID) > 0) return midiOutPorts[ID];

	try {
		midiOutPorts.emplace(ID, new RtMidiOut());
		midiOutPorts[ID]->openPort(ID);
		return midiOutPorts[ID];
	}
	catch (RtMidiError& error) {
		MIDI::GenerateMidiError(error);
	}
}

bool YSE::MIDI::deviceManager::isPrepared()
{
	if (initialized) return true;

	try {
		if (midiIn != nullptr) {
			delete midiIn;
		}
		midiIn = new RtMidiIn();
	}
	catch (RtMidiError& error) {
		GenerateMidiError(error);
		RtMidiError::Type type = error.getType();
		
		// return when this is more than a warning
		if (type != RtMidiError::Type::WARNING && type != RtMidiError::Type::DEBUG_WARNING) {
			delete midiIn;
			midiIn = nullptr;
			return false;
		}
	}
	
	try {
		if (midiOut != nullptr) {
			delete midiOut;
		}
		midiOut = new RtMidiOut();
	}
	catch (RtMidiError& error) {
		GenerateMidiError(error);
		RtMidiError::Type type = error.getType();

		// return when this is more than a warning
		if (type != RtMidiError::Type::WARNING && type != RtMidiError::Type::DEBUG_WARNING) {
			delete midiOut;
			midiOut = nullptr;
			return false;
		}
	}
	
	initialized = true;
	return true;
}


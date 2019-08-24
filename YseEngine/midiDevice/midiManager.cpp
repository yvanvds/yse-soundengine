#include "midiManager.h"
#include "internalHeaders.h"

YSE::MIDIDEVICE::managerObject& YSE::MIDIDEVICE::Manager()
{
	static managerObject d;
	return d;
}

// simple conversion pipe between RtMidi and our log system
void YSE::MIDIDEVICE::GenerateMidiError(const RtMidiError & error)
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

YSE::MIDIDEVICE::managerObject::managerObject()
	: midiIn(nullptr)
	, midiOut(nullptr)
	, initialized(false)
{

}

YSE::MIDIDEVICE::managerObject::~managerObject() {
	if (midiIn != nullptr) delete midiIn;
	if (midiOut != nullptr) delete midiOut;
}

unsigned int YSE::MIDIDEVICE::managerObject::getNumMidiInDevices()
{
	if (isPrepared()) {
		return midiIn->getPortCount();
	}
	return 0;
}

unsigned int YSE::MIDIDEVICE::managerObject::getNumMidiOutDevices()
{
	if (isPrepared()) {
		return midiOut->getPortCount();
	}
	return 0;
}

const std::string YSE::MIDIDEVICE::managerObject::getMidiInDeviceName(unsigned int ID)
{
	if (isPrepared()) {
		return midiIn->getPortName(ID);
	}
	return "Invalid Call";
}

const std::string YSE::MIDIDEVICE::managerObject::getMidiOutDeviceName(unsigned int ID)
{
	if (isPrepared()) {
		return midiOut->getPortName(ID);
	}
	return "Invalid Call";
}

bool YSE::MIDIDEVICE::managerObject::openMidiOutPort(unsigned int ID)
{
	if (isPrepared()) {
		try {
			if (midiOut->isPortOpen()) {
				midiOut->closePort();
			}
			midiOut->openPort(ID);
			return true;
		}
		catch (RtMidiError& error) {
			GenerateMidiError(error);
			return false;
		}
	}
}

void YSE::MIDIDEVICE::managerObject::sendMessage(const MIDI::midiMessage& message)
{
	if (isPrepared()) {
		midiOut->sendMessage(message.getRaw());
	}
}

bool YSE::MIDIDEVICE::managerObject::isPrepared()
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


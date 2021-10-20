#include "headers/defines.hpp"
#if YSE_WINDOWS
#include "device.hpp"
#include "RtMidi.h"
#include "midiDeviceManager.h"

YSE::midiOut::midiOut()
	: device(nullptr) {

}


void YSE::midiOut::create(unsigned int port)
{
	device = MIDI::DeviceManager().getMidiOutPort(port);
}

void YSE::midiOut::NoteOn(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0x90 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::NoteOn(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0x90 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::NoteOff(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0x80 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::NoteOff(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0x80 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::PolyPressure(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xA0 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::PolyPressure(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xA0 + channel;
		message[1] = pitch;
		message[2] = velocity;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::ChannelPressure(MIDI::M_CHANNEL channel, unsigned char value)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xD0 + channel;
		message[1] = value;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::ProgramChange(MIDI::M_CHANNEL channel, unsigned char value)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xC0 + channel;
		message[1] = value;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::ControlChange(MIDI::M_CHANNEL channel, unsigned char controller, unsigned char value)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0 + channel;
		message[1] = controller;
		message[2] = value;
		device->sendMessage(message, 3);
	}
}



void YSE::midiOut::AllNotesOff(MIDI::M_CHANNEL channel)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0 + channel;
		message[1] = 0x7B;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}



void YSE::midiOut::AllNotesOff()
{
	if (isPrepared()) {
		unsigned char message[3];
		message[1] = 0x7B;
		message[2] = 0;

		for (unsigned char i = 0; i < 16; i++) {
			message[0] = 0xB0 + i;
			device->sendMessage(message, 3);
		}
	}
}

void YSE::midiOut::Reset(MIDI::M_CHANNEL channel)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0 + channel;
		message[1] = 0x79;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::Reset()
{
	if (isPrepared()) {
		unsigned char message[3];
		message[1] = 0x79;
		message[2] = 0;

		for (unsigned char i = 0; i < 16; i++) {
			message[0] = 0xB0 + i;
			device->sendMessage(message, 3);
		}
	}
}

void YSE::midiOut::LocalControl(bool on)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0;
		message[1] = 0x7A;
		message[2] = on ? 127 : 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::Omni(bool on)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0;
		message[1] = on ? 0x7D : 0x7C;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::Poly(bool on)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = 0xB0;
		message[1] = on ? 0x7F : 0x7E;
		message[2] = 0;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::Raw(unsigned char a, unsigned char b, unsigned char c)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = a;
		message[1] = b;
		message[2] = c;
		device->sendMessage(message, 3);
	}
}

void YSE::midiOut::Raw(const std::string& value)
{
	if (isPrepared()) {
		unsigned char message[3];
		message[0] = value.length() > 0 ? value[0] : 0;
		message[1] = value.length() > 1 ? value[1] : 0;
		message[2] = value.length()  > 2 ? value[2] : 0;
		device->sendMessage(message, 3);
	}
}





bool YSE::midiOut::isPrepared() {
	return device != nullptr;
}

#endif
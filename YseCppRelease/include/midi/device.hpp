#pragma once
#if YSE_WINDOWS

#include "../headers/defines.hpp"
#include "midiMessage.hpp"
#include "../headers/enums.hpp"

class RtMidiOut;

namespace YSE {
	
	class API midiOut {
	public:
		midiOut();

		midiOut(const midiOut&) = delete;

		void create(unsigned int port);

		void NoteOn(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);
		void NoteOn(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		void NoteOff(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);
		void NoteOff(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		void PolyPressure(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);
		void PolyPressure(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		void ChannelPressure(MIDI::M_CHANNEL channel, unsigned char value);
		void ProgramChange(MIDI::M_CHANNEL channel, unsigned char value);

		void ControlChange(MIDI::M_CHANNEL channel, unsigned char controller, unsigned char value);

		void AllNotesOff(MIDI::M_CHANNEL channel);
		void AllNotesOff();

		void Reset(MIDI::M_CHANNEL channel);
		void Reset();

		void LocalControl(bool on);
		void Omni(bool on);
		void Poly(bool on); // off turns on Mono mode

		void Raw(unsigned char a, unsigned char b, unsigned char c);
		void Raw(const std::string& value);

	private:
		bool isPrepared();

		RtMidiOut* device;
	};

}

#endif
#pragma once

#include "../headers/defines.hpp"
#if YSE_WINDOWS || YSE_LINUX

#include "midiMessage.hpp"
#include "../headers/enums.hpp"

/// @cond INTERNAL
class RtMidiOut;
/// @endcond

namespace YSE {

	/**
	 *  @brief MIDI output port.
	 *
	 *  Open a port with ``create`` (using a device index obtained from
	 *  ``System().getMidiOutDeviceName(...)``), then send MIDI messages to it.
	 *  The class is non-copyable / non-movable — wrap in a ``unique_ptr`` if
	 *  you need transferable ownership.
	 *
	 *  @note Windows / Linux only — backed by RtMidi.
	 */
	class API midiOut {
	public:
		midiOut();

		midiOut(const midiOut&) = delete;
		midiOut& operator=(const midiOut&) = delete;
		midiOut(midiOut&&) = delete;
		midiOut& operator=(midiOut&&) = delete;

		/** @brief Open the MIDI device at the given port index. */
		void create(unsigned int port);

		/** @brief Send a Note-On message with a typed pitch enum. */
		void NoteOn(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);

		/** @brief Send a Note-On message with a raw 0-127 pitch. */
		void NoteOn(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		/** @brief Send a Note-Off message with a typed pitch enum. */
		void NoteOff(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);

		/** @brief Send a Note-Off message with a raw 0-127 pitch. */
		void NoteOff(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		/** @brief Send a polyphonic key-pressure (aftertouch) message with a typed pitch enum. */
		void PolyPressure(MIDI::M_CHANNEL channel, MIDI::M_PITCH pitch, unsigned char velocity);

		/** @brief Send a polyphonic key-pressure (aftertouch) message with a raw 0-127 pitch. */
		void PolyPressure(MIDI::M_CHANNEL channel, unsigned char pitch, unsigned char velocity);

		/** @brief Send a channel-pressure message. */
		void ChannelPressure(MIDI::M_CHANNEL channel, unsigned char value);

		/** @brief Send a program-change message. */
		void ProgramChange(MIDI::M_CHANNEL channel, unsigned char value);

		/** @brief Send a control-change message. */
		void ControlChange(MIDI::M_CHANNEL channel, unsigned char controller, unsigned char value);

		/** @brief Send All-Notes-Off on a specific channel. */
		void AllNotesOff(MIDI::M_CHANNEL channel);

		/** @brief Send All-Notes-Off on every channel. */
		void AllNotesOff();

		/** @brief Send Reset-All-Controllers on a specific channel. */
		void Reset(MIDI::M_CHANNEL channel);

		/** @brief Send Reset-All-Controllers on every channel. */
		void Reset();

		/** @brief Toggle local-keyboard control on the receiving instrument. */
		void LocalControl(bool on);

		/** @brief Toggle Omni mode (receive on all channels) on the receiving instrument. */
		void Omni(bool on);

		/** @brief Toggle Poly (``true``) or Mono (``false``) mode on the receiving instrument. */
		void Poly(bool on);

		/** @brief Send three raw MIDI bytes. */
		void Raw(unsigned char a, unsigned char b, unsigned char c);

		/** @brief Send a raw MIDI byte string. */
		void Raw(const std::string& value);

	private:
		bool isPrepared();

		RtMidiOut* device;
	};

}

#endif
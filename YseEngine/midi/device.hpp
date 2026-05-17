#pragma once

#include "../headers/defines.hpp"
#if YSE_WINDOWS || YSE_LINUX

#include <atomic>
#include <cstddef>
#include <vector>

#include "midiMessage.hpp"
#include "../headers/enums.hpp"

/// @cond INTERNAL
class RtMidiIn;
class RtMidiOut;
struct MidiInDispatchTester;
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

	/**
	 *  @brief MIDI input port.
	 *
	 *  Open a port with ``create`` (using a device index obtained from
	 *  ``System().getMidiInDeviceName(...)``), register one or both of the
	 *  callbacks, then receive MIDI messages as they arrive on the device.
	 *  The class is non-copyable / non-movable — wrap in a ``unique_ptr`` if
	 *  you need transferable ownership.
	 *
	 *  Two callback styles are supported, independently. ``RawCallback``
	 *  delivers the original byte sequence and is the right choice for
	 *  full-protocol coverage (SysEx, clock, song-position). ``ParsedCallback``
	 *  pre-decodes the common case (status nibble / channel nibble / two data
	 *  bytes) and is the right choice for note-in / CC / pitch-bend handlers
	 *  that just need typed fields. Either may be null to disable that path.
	 *
	 *  @note Callbacks fire on RtMidi's internal input thread. They must
	 *        return quickly and may not block on I/O. The raw byte pointer
	 *        is valid only for the duration of the call — copy it if you
	 *        need to deliver it across threads. The C API exposes a
	 *        ``yse_midi_in_*`` mirror that already does that copy for you.
	 *  @note The dispatch path is internally decoupled from the registered
	 *        host callbacks so that future engine-internal subscribers
	 *        (patcher nodes, synth voices) can hook into the same port hub.
	 *        See yvanvds/yse-soundengine#52.
	 *  @note Windows / Linux only — backed by RtMidi.
	 */
	class API midiIn {
	public:
		/** @brief Signature for raw-bytes MIDI input callbacks. */
		using RawCallback    = void (*)(double                timestampSec,
		                                const unsigned char*  bytes,
		                                std::size_t           len,
		                                void*                 user_data);

		/** @brief Signature for parsed MIDI input callbacks (status / channel / two data bytes). */
		using ParsedCallback = void (*)(double                timestampSec,
		                                unsigned char         status,
		                                unsigned char         channel,
		                                unsigned char         data1,
		                                unsigned char         data2,
		                                void*                 user_data);

		midiIn();
		~midiIn();

		midiIn(const midiIn&) = delete;
		midiIn& operator=(const midiIn&) = delete;
		midiIn(midiIn&&) = delete;
		midiIn& operator=(midiIn&&) = delete;

		/** @brief Open the MIDI input device at the given port index. */
		void create(unsigned int port);

		/** @brief Close the currently open port. Safe to call when nothing is open. */
		void close();

		/** @brief Whether a port is currently open. */
		bool isOpen() const;

		/** @brief Install a raw-bytes callback. Pass ``nullptr`` to detach. */
		void setRawCallback(RawCallback cb, void* user_data);

		/** @brief Install a parsed callback. Pass ``nullptr`` to detach. */
		void setParsedCallback(ParsedCallback cb, void* user_data);

	private:
		// RtMidi's setCallback entry point. Forwards to dispatch() so the
		// raw / parsed subscribers are not coupled to the trampoline; this
		// leaves room for future internal subscribers (patcher midiIn node,
		// synth voices) without restructuring the RtMidi wiring.
		static void rtMidiTrampoline(double ts,
		                             std::vector<unsigned char>* msg,
		                             void* userData);

		// Fan-out point — host callbacks today, additive internal subscribers later.
		void dispatch(double timestampSec,
		              const unsigned char* bytes,
		              std::size_t len);

		RtMidiIn* device;

		std::atomic<RawCallback>    rawCb{nullptr};
		std::atomic<void*>          rawUser{nullptr};
		std::atomic<ParsedCallback> parsedCb{nullptr};
		std::atomic<void*>          parsedUser{nullptr};

		// Test-only access to the private dispatch() path. Driven by
		// Tests/midi/test_midi_in.cpp to verify the parsed-callback nibble
		// split and the raw passthrough without needing an RtMidi virtual
		// port (which is unavailable on Windows).
		friend struct ::MidiInDispatchTester;
	};

}

#endif
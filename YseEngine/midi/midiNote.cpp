#include "midiNote.hpp"

YSE::MIDI::midiNote::midiNote(unsigned char note, unsigned velocity)
{
	raw.push_back(144);
	raw.push_back(note);
	raw.push_back(velocity);
}

void YSE::MIDI::midiNote::note(unsigned char note)
{
	raw[1] = note;
}

unsigned char YSE::MIDI::midiNote::note() const
{
	return raw[1];
}

void YSE::MIDI::midiNote::velocity(unsigned char velocity)
{
	raw[2] = velocity;
}

unsigned char YSE::MIDI::midiNote::velocity() const
{
	return raw[2];
}

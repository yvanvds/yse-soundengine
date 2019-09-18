#include "stdafx.h"

#include "Demo16_Midi.h"
#include <iostream>

DemoMidi::DemoMidi()
	: firstNote(true)
{
	SetTitle("Midi Functions");

	AddAction('1', "Print MIDI Ports", std::bind(&DemoMidi::PrintMidiPorts, this));
	AddAction('2', "Play a note", std::bind(&DemoMidi::PlayNote, this));
	AddAction('3', "All Notes Off", std::bind(&DemoMidi::AllNotesOff, this));

	output1.create(1);
	output2.create(1);
}

DemoMidi::~DemoMidi() {
	output1.AllNotesOff();
	output2.AllNotesOff();
}

void DemoMidi::PrintMidiPorts()
{
	int in = YSE::System().getNumMidiInDevices();
	int out = YSE::System().getNumMidiOutDevices();

	std::cout << "There are " << in << "Midi In Ports" << std::endl;
	for (int i = 0; i < in; i++) {
		std::cout << "  Port " << i << ": " << YSE::System().getMidiInDeviceName(i) << std::endl;
	}

	std::cout << "There are " << out << "Midi Out Ports" << std::endl;
	for (int i = 0; i < out; i++) {
		std::cout << "  Port " << i << ": " << YSE::System().getMidiOutDeviceName(i) << std::endl;
	}
}

void DemoMidi::PlayNote() {
	if (firstNote) {
		
		
		output1.NoteOn(YSE::MIDI::CH_01, 60, 80);
		output2.NoteOn(YSE::MIDI::CH_01, YSE::MIDI::D4, 80);

		firstNote = false;
	}
	else {

		output1.NoteOn(YSE::MIDI::CH_01, 60, 0);
		output2.NoteOn(YSE::MIDI::CH_01, YSE::MIDI::D4, 0);
		
		output1.NoteOn(YSE::MIDI::CH_01, 60, 80);
		output2.NoteOn(YSE::MIDI::CH_01, YSE::MIDI::D4, 80);
	}
}

void DemoMidi::AllNotesOff() {
	output1.AllNotesOff();
	output2.AllNotesOff();

	output1.NoteOn(YSE::MIDI::CH_01, 60, 0);
	output2.NoteOn(YSE::MIDI::CH_01, YSE::MIDI::D4, 0);
}


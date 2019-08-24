#include "stdafx.h"

#include "Demo16_Midi.h"
#include <iostream>

DemoMidi::DemoMidi()
	: note(60, 80)
	, firstNote(true)
{
	SetTitle("Midi Functions");

	AddAction('1', "Print MIDI Ports", std::bind(&DemoMidi::PrintMidiPorts, this));
	AddAction('2', "Play a note", std::bind(&DemoMidi::PlayNote, this));
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
		if (YSE::System().openMidiOutPort(1)) {
			YSE::System().sendMidi(note);
			firstNote = false;
			std::cout << "playing note " << note.note() << std::endl;
		}
		else {
			std::cout << "cannot open midi out port 0";
		}
	}
	else {
		note.velocity(0);
		YSE::System().sendMidi(note);
		note.note(YSE::Random(50, 70));
		note.velocity(80);
		YSE::System().sendMidi(note);
		std::cout << "playing note " << note.note() << std::endl;
	}
}


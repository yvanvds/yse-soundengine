#include "stdafx.h"

#include "Demo17_MidiPatcher.h"

using namespace YSE;

DemoMidiPatcher::DemoMidiPatcher() {
	SetTitle("Midi Patcher Demo");
	AddAction('1', "Note On", std::bind(&DemoMidiPatcher::NoteOn, this));
	AddAction('2', "Last Note Off", std::bind(&DemoMidiPatcher::NoteOff, this));
	AddAction('3', "All Notes Off", std::bind(&DemoMidiPatcher::AllNotesOff, this));

	Setup();

	sound.create(patcher);
	sound.play();
}

void DemoMidiPatcher::NoteOn()
{
	if (currentNote != 0) {
		NoteOff();
	}
	currentNote = Random(60, 80);
	patcher.PassData(Random(40, 127), "velOn");
	patcher.PassData(currentNote, "noteOn");
}

void DemoMidiPatcher::NoteOff()
{
	patcher.PassData(currentNote, "noteOff");
}

void DemoMidiPatcher::AllNotesOff()
{
	patcher.PassBang("allOff");
}

void DemoMidiPatcher::Setup()
{
	patcher.create(1);

	midiOut = patcher.CreateObject(OBJ::M_OUT);
	noteOn = patcher.CreateObject(OBJ::M_NOTEON);
	noteOff = patcher.CreateObject(OBJ::M_NOTEOFF);

	controlNoteOn = patcher.CreateObject(OBJ::G_RECEIVE, "noteOn");
	controlNoteOff = patcher.CreateObject(OBJ::G_RECEIVE, "noteOff");
	controlVelocityOn = patcher.CreateObject(OBJ::G_RECEIVE, "velOn");
	controlAllOff = patcher.CreateObject(OBJ::G_RECEIVE, "allOff");
	
	allNotesOff = patcher.CreateObject(OBJ::G_MESSAGE);
	allNotesOff->SetParams("allnotesoff");

	patcher.Connect(noteOn, 0, midiOut, 0);
	patcher.Connect(noteOff, 0, midiOut, 0);

	patcher.Connect(controlNoteOn, 0, noteOn, 0);
	patcher.Connect(controlVelocityOn, 0, noteOn, 1);

	patcher.Connect(controlNoteOff, 0, noteOff, 0);
	patcher.Connect(allNotesOff, 0, midiOut, 0);

	patcher.Connect(controlAllOff, 0, allNotesOff, 0);
}

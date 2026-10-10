
#include "stdafx.h"

#include "Demo15_RestartAudio.h"
#include <iostream>

DemoRestartAudio::DemoRestartAudio() {
	sound.create(YSE_TEST_RESOURCES_DIR "/drone.ogg", nullptr, true);
	sound.play();
	

	SetTitle("Restart Audio");

	AddAction('1', "Audio On", std::bind(&DemoRestartAudio::AudioOn, this));
	AddAction('2', "Audio Off", std::bind(&DemoRestartAudio::AudioOff, this));
	AddAction('3', "Reconnect", std::bind(&DemoRestartAudio::Reconnect, this));
	AddAction('4', "Reconnect after 2 s", std::bind(&DemoRestartAudio::ReconnectIn2Seconds, this));
	AddAction('5', "Don't Reconnect", std::bind(&DemoRestartAudio::DontReconnect, this));
}

DemoRestartAudio::~DemoRestartAudio() {
	YSE::System().autoReconnect(false, 0);
}

void DemoRestartAudio::AudioOn() {
	YSE::System().resume();
}

void DemoRestartAudio::AudioOff() {
	YSE::System().pause();
}

void DemoRestartAudio::Reconnect() {
	YSE::System().autoReconnect(true, 0);
}

void DemoRestartAudio::ReconnectIn2Seconds() {
	// Milliseconds (issue #681). This used to read 20, meaning 20 consecutive
	// update() ticks — a wait this demo's frame rate defined, not the caller.
	YSE::System().autoReconnect(true, 2000);
}

void DemoRestartAudio::DontReconnect() {
	YSE::System().autoReconnect(false, 0);
}

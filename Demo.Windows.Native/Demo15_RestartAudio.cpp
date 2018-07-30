
#include "stdafx.h"

#include "Demo15_RestartAudio.h"
#include <iostream>

DemoRestartAudio::DemoRestartAudio() {
	sound.create("..\\TestResources\\drone.ogg", nullptr, true);
	sound.play();
	

	SetTitle("Restart Audio");

	AddAction('1', "Audio On", std::bind(&DemoRestartAudio::AudioOn, this));
	AddAction('2', "Audio Off", std::bind(&DemoRestartAudio::AudioOff, this));
	AddAction('3', "Reconnect", std::bind(&DemoRestartAudio::Reconnect, this));
	AddAction('4', "Reconnect in 20", std::bind(&DemoRestartAudio::ReconnectIn20, this));
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

void DemoRestartAudio::ReconnectIn20() {
	YSE::System().autoReconnect(true, 20);
}

void DemoRestartAudio::DontReconnect() {
	YSE::System().autoReconnect(false, 0);
}

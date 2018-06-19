#pragma once
#include "basePage.h"
#include "yse.hpp"

class DemoRestartAudio : public basePage {
public:
	DemoRestartAudio();
	~DemoRestartAudio();

	void AudioOn();
	void AudioOff();

	void Reconnect();
	void ReconnectIn20();
	void DontReconnect();

private:
	YSE::sound sound;
};
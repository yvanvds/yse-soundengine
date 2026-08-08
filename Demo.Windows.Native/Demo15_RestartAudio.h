
#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

class DemoRestartAudio : public basePage {
public:
	DemoRestartAudio();
	~DemoRestartAudio();

	void AudioOn();
	void AudioOff();

	void Reconnect();
	// autoReconnect's delay is milliseconds since issue #681 (it used to be a
	// count of update() ticks), so the delayed variant asks for a real 2 s.
	void ReconnectIn2Seconds();
	void DontReconnect();

private:
	YSE::sound sound;
};

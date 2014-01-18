#pragma once
#if defined(USE_PORTAUDIO)
#include "portaudio.h"
#include "internal/soundimpl.h"
#include "device.hpp"
#include <string>
#include <vector>

namespace YSE {
	struct device {
    UInt ID;
		Bool defaultIn;
		Bool defaultOut;
		std::string host;
		std::string name;
		Int inChannels;
		Int outChannels;
		Int sampleRate;
		Dbl lowInputLatency;
		Dbl highInputLatency;
		Dbl lowOutputLatency;
		Dbl highOutputLatency;

		// asio values
		PaHostApiTypeId type;
		Int minLatency, maxLatency;
		Int preferredLatency;

		device();
	};

	struct systemDevice {
		Bool initialize();
		~systemDevice();

		std::vector<device> devices;
        std::vector<audioDevice> deviceList;
		Bool updateDevices();
		Bool openDevice(UInt ID, Int outChannels);
		Int activeDevice;

		void close();

		std::string getError();
		Flt cpuLoad();

		static Int callback(const void *in, void *out, unsigned long length, const PaStreamCallbackTimeInfo* time, PaStreamCallbackFlags flags, void *userData);
        inline void sleep(Int ms) { Pa_Sleep(ms); } // use pa_sleep (usefull for console applications and called from system)
	private:
		PaError _lastError;
		PaStream * _stream;
		Bool init;
		Bool open;
		Bool started;
	};

}

#endif // USE_PORTAUDIO

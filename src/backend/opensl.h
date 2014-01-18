#pragma once
#if defined(USE_OPENSL)
#include <string>
#include <vector>
#include "../device.hpp"

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
	Int type;
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

	static Int callback();
    inline void sleep(Int ms) {} // use pa_sleep (usefull for console applications and called from system)
private:
	int _lastError;
	void * _stream;
	Bool init;
	Bool open;
	Bool started;
  };
};




#endif // OPENSL

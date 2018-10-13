using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
	public enum ChannelType
	{
		Auto,
		Mono,
		Stereo,
		Quad,
		Surround51,
		Surround51Side,
		Surround61,
		Surround71,
		Custom,
	}

	public interface IDeviceSetup
	{
		void SetInput(IDevice device);
		void SetOutput(IDevice device);
		void SetSampleRate(double value);
		void SetBufferSize(int value);

		int GetOutputChannels();
	}
}

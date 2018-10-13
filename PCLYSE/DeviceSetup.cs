using System;
using System.Collections.Generic;
using System.Text;
using IYse;

namespace YSE
{
	public class DeviceSetup : Yse.deviceSetup, IYse.IDeviceSetup
	{
		public int GetOutputChannels()
		{
			return getOutputChannels();
		}

		public void SetBufferSize(int value)
		{
			setBufferSize(value);
		}

		public void SetInput(IDevice device)
		{
			setInput((device as Device).device);
		}

		public void SetOutput(IDevice device)
		{
			setOutput((device as Device).device);
		}

		public void SetSampleRate(double value)
		{
			setSampleRate(value);
		}
	}
}

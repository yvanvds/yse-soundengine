using System;
using System.Collections.Generic;
using System.Text;

namespace YSE
{
	public class Device : IYse.IDevice
	{
		internal Yse.device device;
		private List<string> inputChannels = new List<string>();
		private List<string> outputChannels = new List<string>();
		private List<double> samplerates = new List<double>();
		private List<int> buffersizes = new List<int>();

		public Device(Yse.device device)
		{
			this.device = device;

			for(uint i = 0; i < device.getNumInputChannelNames(); i++)
			{
				inputChannels.Add(device.getInputChannelName(i));
			}

			for(uint i = 0; i < device.getNumOutputChannelNames(); i++)
			{
				outputChannels.Add(device.getOutputChannelName(i));
			}

			for(uint i = 0; i < device.getNumAvailableSampleRates(); i++)
			{
				samplerates.Add(device.getAvailableSampleRate(i));
			}

			for(uint i = 0; i < device.getNumAvailableBufferSizes(); i++)
			{
				buffersizes.Add(device.getAvailableBufferSize(i));
			}
		}

		public string Name => device.getName();

		public string TypeName => device.getTypeName();

		public int ID => device.getID();

		public IReadOnlyCollection<string> InputChannels => inputChannels.AsReadOnly();

		public IReadOnlyCollection<string> OutputChannels => outputChannels.AsReadOnly();

		public IReadOnlyCollection<double> SampleRates => samplerates.AsReadOnly();

		public IReadOnlyCollection<int> BufferSizes => buffersizes.AsReadOnly();

		public int DefaultBufferSize => device.getDefaultBufferSize();

		public int OutputLatency => device.getOutputLatency();

		public int InputLatency => device.getInputLatency();
	}
}

using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{
	public interface IDevice
	{
		string Name { get; }
		string TypeName { get; }
		int ID { get; }

		IReadOnlyCollection<string> InputChannels { get; }
		IReadOnlyCollection<string> OutputChannels { get; }

		IReadOnlyCollection<double> SampleRates { get; }
		IReadOnlyCollection<int> BufferSizes { get; }

		int DefaultBufferSize { get; }
		int OutputLatency { get; }
		int InputLatency { get; }
	}
}

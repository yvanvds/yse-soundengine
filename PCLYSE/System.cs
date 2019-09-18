using System;
using System.Collections.Generic;
using System.Text;
using YSE;


namespace YSE
{
    public class System : IYse.ISystem
    {

        public uint NumDevices
        {
            get => Yse.Yse.System().getNumDevices();
        }

        public IYse.IDevice GetDevice(uint nr)
        {
            return new Device(Yse.Yse.System().getDevice(nr));
        }

        public void OpenDevice(IYse.IDeviceSetup setup, IYse.ChannelType channeltype = IYse.ChannelType.Auto)
        {
            Yse.CHANNEL_TYPE type = Yse.CHANNEL_TYPE.CT_AUTO;

            switch (channeltype)
            {
                case IYse.ChannelType.Custom: type = Yse.CHANNEL_TYPE.CT_CUSTOM; break;
                case IYse.ChannelType.Mono: type = Yse.CHANNEL_TYPE.CT_MONO; break;
                case IYse.ChannelType.Quad: type = Yse.CHANNEL_TYPE.CT_QUAD; break;
                case IYse.ChannelType.Stereo: type = Yse.CHANNEL_TYPE.CT_STEREO; break;
                case IYse.ChannelType.Surround51: type = Yse.CHANNEL_TYPE.CT_51; break;
                case IYse.ChannelType.Surround51Side: type = Yse.CHANNEL_TYPE.CT_51SIDE; break;
                case IYse.ChannelType.Surround61: type = Yse.CHANNEL_TYPE.CT_61; break;
                case IYse.ChannelType.Surround71: type = Yse.CHANNEL_TYPE.CT_71; break;
            }

            Yse.Yse.System().openDevice(setup as Yse.deviceSetup, type);
        }

        public void CloseCurrentDevice()
        {
            Yse.Yse.System().closeCurrentDevice();
        }

        public string DefaultDevice => Yse.Yse.System().getDefaultDevice();
        public string DefaultHost => Yse.Yse.System().getDefaultHost();

        public uint NumMidiInDevices => Yse.Yse.System().getNumMidiInDevices();
        public uint NumMidiOutDevices => Yse.Yse.System().getNumMidiOutDevices();
        public string MidiInDeviceName(uint ID) { return Yse.Yse.System().getMidiInDeviceName(ID); }
        public string MidiOutDeviceName(uint ID) { return Yse.Yse.System().getMidiOutDeviceName(ID); }

        public int MaxSounds { get => Yse.Yse.System().maxSounds(); set => Yse.Yse.System().maxSounds(value); }

        public float CpuLoad
        {
            get
            {
                return Yse.Yse.System().cpuLoad();
            }
        }

        bool IYse.ISystem.AudioTest
        {
            get => AudioTestOn;
            set
            {
                Yse.Yse.System().AudioTest(value);
                AudioTestOn = value;
            }
        }

        public string Version => Yse.Yse.System().Version();

        public void Close()
        {
            Yse.Yse.System().close();
        }

        public bool Init()
        {
            return Yse.Yse.System().init();
        }

        public void Update()
        {
            Yse.Yse.System().update();
        }

        public IYse.IReverb GetReverb()
        {
            return new YSE.Reverb(Yse.Yse.System().getGlobalReverb());
        }

        public void AutoReconnect(bool on, int delay)
        {
            Yse.Yse.System().autoReconnect(on, delay);
        }

        public int MissedCallbacks()
        {
            return Yse.Yse.System().missedCallbacks();
        }

        public void Pause()
        {
            Yse.Yse.System().pause();
        }

        public void Resume()
        {
            Yse.Yse.System().resume();
        }

        private bool AudioTestOn = false;

    }
}

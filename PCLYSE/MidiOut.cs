using System;
using System.Collections.Generic;
using System.Text;
using IYse;

namespace YSE
{
    public class MidiOut : Yse.midiOut, IYse.IMidiOut
    {
        bool IMidiOut.LocalControl { set => LocalControl(value); }
        bool IMidiOut.Omni { set => Omni(value); }
        bool IMidiOut.Poly { set => Poly(value); }

        public void AllNotesOff(M_CHANNEL channel)
        {
            base.AllNotesOff(Utils.Convert(channel));
        }

        public void ChannelPressure(M_CHANNEL channel, byte value)
        {
            base.ChannelPressure(Utils.Convert(channel), value);
        }

        public void ControlChange(M_CHANNEL channel, byte controller, byte value)
        {
            ControlChange(Utils.Convert(channel), controller, value);
        }

        public void Create(byte port)
        {
            base.create(port);
        }

        public void NoteOff(M_CHANNEL channel, byte pitch, byte velocity)
        {
            base.NoteOff(Utils.Convert(channel), pitch, velocity);
        }

        public void NoteOn(M_CHANNEL channel, byte pitch, byte velocity)
        {
            NoteOn(Utils.Convert(channel), pitch, velocity);
        }

        public void PolyPressure(M_CHANNEL channel, byte pitch, byte velocity)
        {
            PolyPressure(Utils.Convert(channel), pitch, velocity);
        }

        public void ProgramChange(M_CHANNEL channel, byte value)
        {
            ProgramChange(Utils.Convert(channel), value);
        }

        public void Reset(M_CHANNEL channel)
        {
            Reset(Utils.Convert(channel));
        }
    }

    static class Utils
    {
        public static Yse.M_CHANNEL Convert(IYse.M_CHANNEL channel)
        {
            switch(channel)
            {
                case M_CHANNEL.CH01: return Yse.M_CHANNEL.CH_01;
                case M_CHANNEL.CH02: return Yse.M_CHANNEL.CH_02;
                case M_CHANNEL.CH03: return Yse.M_CHANNEL.CH_03;
                case M_CHANNEL.CH04: return Yse.M_CHANNEL.CH_04;
                case M_CHANNEL.CH05: return Yse.M_CHANNEL.CH_05;
                case M_CHANNEL.CH06: return Yse.M_CHANNEL.CH_06;
                case M_CHANNEL.CH07: return Yse.M_CHANNEL.CH_07;
                case M_CHANNEL.CH08: return Yse.M_CHANNEL.CH_08;
                case M_CHANNEL.CH09: return Yse.M_CHANNEL.CH_09;
                case M_CHANNEL.CH10: return Yse.M_CHANNEL.CH_10;
                case M_CHANNEL.CH11: return Yse.M_CHANNEL.CH_11;
                case M_CHANNEL.CH12: return Yse.M_CHANNEL.CH_12;
                case M_CHANNEL.CH13: return Yse.M_CHANNEL.CH_13;
                case M_CHANNEL.CH14: return Yse.M_CHANNEL.CH_14;
                case M_CHANNEL.CH15: return Yse.M_CHANNEL.CH_15;
                case M_CHANNEL.CH16: return Yse.M_CHANNEL.CH_16;

                default: return Yse.M_CHANNEL.CH_01;
            }
            
        }
    }
    
}

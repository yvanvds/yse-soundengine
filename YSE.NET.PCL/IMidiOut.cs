using System;
using System.Collections.Generic;
using System.Text;

namespace IYse
{ 
    public enum M_CHANNEL
    {
        CH01,
        CH02,
        CH03,
        CH04,
        CH05,
        CH06,
        CH07,
        CH08,
        CH09,
        CH10,
        CH11,
        CH12,
        CH13,
        CH14,
        CH15,
        CH16,
    }

    public interface IMidiOut
    {
        void Create(byte port);

        void NoteOn(M_CHANNEL channel, byte pitch, byte velocity);
        void NoteOff(M_CHANNEL channel, byte pitch, byte velocity);
        void PolyPressure(M_CHANNEL channel, byte pitch, byte velocity);
        void ChannelPressure(M_CHANNEL channel, byte value);
        void ProgramChange(M_CHANNEL channel, byte value);
        void ControlChange(M_CHANNEL channel, byte controller, byte value);
        void AllNotesOff(M_CHANNEL channel);
        void AllNotesOff();
        void Reset(M_CHANNEL channel);
        void Reset();
        bool LocalControl { set; }
        bool Omni { set; }
        bool Poly { set; }
        void Raw(byte a, byte b, byte c);
    }
}

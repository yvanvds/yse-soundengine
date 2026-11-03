// Sound files with instrument data and loops, for the tests of
// DSP::sampleBuffer::info and ~info (issue #1138).
//
// WavWithLoops goes through libsndfile, which writes a WAV smpl chunk from
// SFC_SET_INSTRUMENT. libsndfile does not write an AIFF INST chunk, so
// AiffWithInstrument builds the file byte by byte: FORM / COMM / MARK / INST
// / SSND, 16-bit, big-endian, with the sustain and release loops pointing at
// markers, the way a sampler saves one.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <sndfile.hh>

namespace TestHelpers {

  // A loop as a file states it, in frames. mode 0 is "no loop".
  struct FixtureLoop {
    int mode = 1; // AIFF: 0 none, 1 forward, 2 forward/backward
    std::uint32_t start = 0;
    std::uint32_t end = 0;
  };

  // A WAV file, 32-bit float, every sample 0.25, with a smpl chunk holding
  // `loops` (at most two are written by this helper's callers).
  inline bool WavWithLoops(const std::string& path, int channels, int frames, int rate,
                           int baseNote, int detune, const std::vector<FixtureLoop>& loops) {
    SndfileHandle file(path, SFM_WRITE, SF_FORMAT_WAV | SF_FORMAT_FLOAT, channels, rate);
    if (!file) return false;
    SF_INSTRUMENT inst{};
    inst.basenote = static_cast<char>(baseNote);
    inst.detune = static_cast<char>(detune);
    inst.velocity_lo = 0;
    inst.velocity_hi = 127;
    inst.key_lo = 0;
    inst.key_hi = 127;
    inst.gain = 1;
    inst.loop_count = static_cast<int>(loops.size());
    for (std::size_t i = 0; i < loops.size() && i < 16; ++i) {
      inst.loops[i].mode = loops[i].mode == 2 ? SF_LOOP_ALTERNATING : SF_LOOP_FORWARD;
      inst.loops[i].start = loops[i].start;
      inst.loops[i].end = loops[i].end;
      inst.loops[i].count = 0;
    }
    if (file.command(SFC_SET_INSTRUMENT, &inst, sizeof(inst)) != SF_TRUE) return false;
    const std::vector<float> samples(static_cast<std::size_t>(channels) * frames, 0.25f);
    return file.writef(samples.data(), frames) == frames;
  }

  // The instrument fields of an AIFF INST chunk.
  struct FixtureInstrument {
    int baseNote = 60;
    int detune = 0; // cents, -50..50
    int keyLow = 0;
    int keyHigh = 127;
    int velocityLow = 1;
    int velocityHigh = 127;
    int gain = 0; // dB
    FixtureLoop sustain;
    FixtureLoop release;
  };

  namespace detail {
    struct beWriter {
      std::vector<unsigned char> bytes;
      void u8(int v) {
        bytes.push_back(static_cast<unsigned char>(v & 0xff));
      }
      void u16(int v) {
        u8(v >> 8);
        u8(v);
      }
      void u32(std::uint32_t v) {
        u16(static_cast<int>(v >> 16));
        u16(static_cast<int>(v & 0xffff));
      }
      void id(const char* four) {
        for (int i = 0; i < 4; ++i)
          u8(four[i]);
      }
      void chunk(const char* four, const beWriter& body) {
        id(four);
        u32(static_cast<std::uint32_t>(body.bytes.size()));
        bytes.insert(bytes.end(), body.bytes.begin(), body.bytes.end());
        if ((body.bytes.size() & 1) != 0) u8(0);
      }
    };
  } // namespace detail

  // A 16-bit AIFF file of silence with an INST chunk and four markers for
  // its loops. `rate` is a whole number of Hz (not 0), written as an 80-bit
  // extended float.
  inline bool AiffWithInstrument(const std::string& path, int channels, int frames,
                                 std::uint32_t rate, const FixtureInstrument& inst) {
    detail::beWriter comm;
    comm.u16(channels);
    comm.u32(static_cast<std::uint32_t>(frames));
    comm.u16(16);
    int exponent = 31;
    while (exponent > 0 && (rate & (1u << exponent)) == 0)
      exponent--;
    comm.u16(16383 + exponent);
    const std::uint64_t mantissa = static_cast<std::uint64_t>(rate) << (63 - exponent);
    comm.u32(static_cast<std::uint32_t>(mantissa >> 32));
    comm.u32(static_cast<std::uint32_t>(mantissa & 0xffffffffu));

    detail::beWriter mark;
    mark.u16(4);
    const std::uint32_t positions[4] = {inst.sustain.start, inst.sustain.end, inst.release.start,
                                        inst.release.end};
    for (int i = 0; i < 4; ++i) {
      mark.u16(i + 1);
      mark.u32(positions[i]);
      mark.u8(1); // a one-letter name, so the pstring is even with its count
      mark.u8('a' + i);
    }

    detail::beWriter body;
    body.u8(inst.baseNote);
    body.u8(inst.detune);
    body.u8(inst.keyLow);
    body.u8(inst.keyHigh);
    body.u8(inst.velocityLow);
    body.u8(inst.velocityHigh);
    body.u16(inst.gain);
    body.u16(inst.sustain.mode);
    body.u16(1);
    body.u16(2);
    body.u16(inst.release.mode);
    body.u16(3);
    body.u16(4);

    detail::beWriter ssnd;
    ssnd.u32(0);
    ssnd.u32(0);
    for (int i = 0; i < frames * channels; ++i)
      ssnd.u16(0);

    detail::beWriter form;
    form.id("AIFF");
    form.chunk("COMM", comm);
    form.chunk("MARK", mark);
    form.chunk("INST", body);
    form.chunk("SSND", ssnd);
    detail::beWriter file;
    file.chunk("FORM", form);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.bytes.data()),
              static_cast<std::streamsize>(file.bytes.size()));
    return static_cast<bool>(out);
  }

} // namespace TestHelpers

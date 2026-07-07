// Tests for YSE::SYNTH::dx7SysEx (issue #177) — the DX7 SysEx bank importer.
//
// Coverage:
//   - a 32-voice packed bulk dump parses to 32 voices with correct names,
//   - header-variant tolerance (arbitrary SysEx channel, leading/trailing
//     junk around the F0…F7 block, bare unframed payloads),
//   - checksum validation (a corrupted payload is rejected),
//   - corrupt-input robustness (truncated / unrecognised length → no crash),
//   - a single-voice unpacked dump round-trips,
//   - ACCEPTANCE (#177): a parsed voice audibly matches its in-code #176
//     equivalent — the fmVoice rendered from the parsed patch is sample-for-
//     sample identical to the fmVoice rendered from fmPatch::fm2op().
//
// The fixtures are generated deterministically in-test by packing the #176
// built-in voices into the documented DX7 SysEx layout — no copyrighted
// factory bank is committed.

#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dsp/fm/dx7Sysex.hpp"
#include "dsp/fm/fmPatch.hpp"
#include "dsp/fm/fmVoice.hpp"
#include "support/audio_helpers.hpp"

TEST_SUITE("dsp") {

  using YSE::SOUND_STATUS;
  using YSE::SYNTH::dx7Bank;
  using YSE::SYNTH::dx7SysEx;
  using YSE::SYNTH::fmOperator;
  using YSE::SYNTH::fmPatch;
  using YSE::SYNTH::fmVoice;

  // ─── in-test fixture generation (inverse of the importer's unpackers) ──────

  // Pack one fmPatch into its 128-byte DX7 packed-voice representation.
  static void packVoice(const fmPatch& p, uint8_t out[128]) {
    std::memset(out, 0, 128);
    for (int op = 0; op < 6; ++op) {
      const fmOperator& o = p.op[op];
      uint8_t* d = out + op * 17;
      for (int i = 0; i < 4; ++i) {
        d[i] = o.egRate[i];
        d[4 + i] = o.egLevel[i];
      }
      d[8] = o.levelScaleBreakPoint;
      d[9] = o.levelScaleLeftDepth;
      d[10] = o.levelScaleRightDepth;
      d[11] = (o.levelScaleLeftCurve & 0x03) | ((o.levelScaleRightCurve & 0x03) << 2);
      d[12] = (o.rateScaling & 0x07) | ((o.detune & 0x0f) << 3);
      d[13] = (o.ampModSens & 0x03) | ((o.keyVelSens & 0x07) << 2);
      d[14] = o.outputLevel;
      d[15] = (o.oscMode & 0x01) | ((o.freqCoarse & 0x1f) << 1);
      d[16] = o.freqFine;
    }
    uint8_t* g = out + 102;
    for (int i = 0; i < 4; ++i) {
      g[i] = p.pitchEgRate[i];
      g[4 + i] = p.pitchEgLevel[i];
    }
    g[8] = p.algorithm & 0x1f;
    g[9] = (p.feedback & 0x07) | ((p.oscKeySync & 0x01) << 3);
    g[10] = p.lfoSpeed;
    g[11] = p.lfoDelay;
    g[12] = p.lfoPitchModDepth;
    g[13] = p.lfoAmpModDepth;
    g[14] = (p.lfoSync & 0x01) | ((p.lfoWaveform & 0x07) << 1) | ((p.pitchModSens & 0x07) << 4);
    g[15] = p.transpose;
    for (int i = 0; i < 10; ++i)
      g[16 + i] = static_cast<uint8_t>(p.name[i]);
  }

  // Pack one fmPatch into its 155-byte DX7 unpacked-voice representation.
  static void packUnpackedVoice(const fmPatch& p, uint8_t out[155]) {
    std::memset(out, 0, 155);
    for (int op = 0; op < 6; ++op) {
      const fmOperator& o = p.op[op];
      uint8_t* s = out + op * 21;
      for (int i = 0; i < 4; ++i) {
        s[i] = o.egRate[i];
        s[4 + i] = o.egLevel[i];
      }
      s[8] = o.levelScaleBreakPoint;
      s[9] = o.levelScaleLeftDepth;
      s[10] = o.levelScaleRightDepth;
      s[11] = o.levelScaleLeftCurve;
      s[12] = o.levelScaleRightCurve;
      s[13] = o.rateScaling;
      s[14] = o.ampModSens;
      s[15] = o.keyVelSens;
      s[16] = o.outputLevel;
      s[17] = o.oscMode;
      s[18] = o.freqCoarse;
      s[19] = o.freqFine;
      s[20] = o.detune;
    }
    for (int i = 0; i < 4; ++i) {
      out[126 + i] = p.pitchEgRate[i];
      out[130 + i] = p.pitchEgLevel[i];
    }
    out[134] = p.algorithm;
    out[135] = p.feedback;
    out[136] = p.oscKeySync;
    out[137] = p.lfoSpeed;
    out[138] = p.lfoDelay;
    out[139] = p.lfoPitchModDepth;
    out[140] = p.lfoAmpModDepth;
    out[141] = p.lfoSync;
    out[142] = p.lfoWaveform;
    out[143] = p.pitchModSens;
    out[144] = p.transpose;
    for (int i = 0; i < 10; ++i)
      out[145 + i] = static_cast<uint8_t>(p.name[i]);
  }

  static uint8_t sysexChecksum(const uint8_t* p, size_t n) {
    unsigned sum = 0;
    for (size_t i = 0; i < n; ++i)
      sum += p[i];
    return static_cast<uint8_t>((0x80u - (sum & 0x7fu)) & 0x7fu);
  }

  // Wrap a raw payload in the DX7 SysEx frame for the given format byte.
  static std::vector<uint8_t> frame(uint8_t format, uint8_t channel, const uint8_t* payload,
                                    size_t n) {
    std::vector<uint8_t> msg;
    msg.push_back(0xF0);
    msg.push_back(0x43);
    msg.push_back(static_cast<uint8_t>(channel & 0x0f));
    msg.push_back(format);
    msg.push_back(static_cast<uint8_t>((n >> 7) & 0x7f)); // byte-count MS
    msg.push_back(static_cast<uint8_t>(n & 0x7f)); // byte-count LS
    msg.insert(msg.end(), payload, payload + n);
    msg.push_back(sysexChecksum(payload, n));
    msg.push_back(0xF7);
    return msg;
  }

  // Build a full 32-voice packed bulk-dump image. voices[i] fills slot i;
  // remaining slots are filled with fmPatch::sine().
  static std::vector<uint8_t> makeBank(const std::vector<fmPatch>& voices, uint8_t channel = 0) {
    std::vector<uint8_t> payload(4096);
    fmPatch filler = fmPatch::sine();
    for (int i = 0; i < 32; ++i) {
      const fmPatch& src = (static_cast<size_t>(i) < voices.size()) ? voices[i] : filler;
      packVoice(src, payload.data() + i * 128);
    }
    return frame(0x09, channel, payload.data(), payload.size());
  }

  // Render N samples of the sustaining tone of a voice loaded with `patch`.
  static void render(const fmPatch& patch, YSE::DSP::buffer& out, unsigned N) {
    fmVoice v;
    v.setPatch(patch);
    v.frequency(57.f); // A3
    v.velocity(1.f);
    SOUND_STATUS intent = YSE::SS_WANTSTOPLAY;
    v.process(intent);
    for (int i = 0; i < 8; ++i)
      v.process(intent); // warm-up
    const unsigned block = YSE::STANDARD_BUFFERSIZE;
    unsigned filled = 0;
    while (filled + block <= N) {
      v.process(intent);
      out.copyFrom(v.samples[0], 0, filled, block);
      filled += block;
    }
  }

  // ─── parsing a full packed bank ────────────────────────────────────────────

  TEST_CASE("dx7SysEx: 32-voice packed bank parses to 32 named voices") {
    std::vector<fmPatch> v = {fmPatch::fm2op(), fmPatch::brass()};
    std::vector<uint8_t> bank = makeBank(v);

    dx7Bank out;
    REQUIRE(dx7SysEx::parse(bank.data(), bank.size(), out));
    CHECK(out.size() == 32);
    CHECK_FALSE(out.empty());
    CHECK(out.name(0) == "YSE 2opFM");
    CHECK(out.name(1) == "YSE Brass");
    CHECK(out.name(2) == "YSE Sine"); // filler
    CHECK(out.indexOf("YSE Brass") == 1);
    CHECK(out.indexOf("nope") == -1);
    CHECK(out.name(99).empty()); // out of range
  }

  // ─── ACCEPTANCE: parsed patch matches its in-code #176 equivalent ─────────

  TEST_CASE("dx7SysEx: parsed voice renders identically to the in-code fm2op patch") {
    const fmPatch inCode = fmPatch::fm2op();
    std::vector<uint8_t> bank = makeBank({inCode});

    dx7Bank out;
    REQUIRE(dx7SysEx::parse(bank.data(), bank.size(), out));
    REQUIRE(out.size() == 32);

    const unsigned N = 2048;
    YSE::DSP::buffer ref(N), got(N);
    ref = 0.f;
    got = 0.f;
    render(inCode, ref, N);
    render(out.voices[0], got, N);

    CHECK(TestHelpers::measureRms(ref) > 0.005f); // the reference actually sounds
    float* r = ref.getPtr();
    float* g = got.getPtr();
    float maxDiff = 0.f;
    for (unsigned i = 0; i < N; ++i)
      maxDiff = std::max(maxDiff, std::abs(r[i] - g[i]));
    // The DX7 round-trip is loss-free for in-range parameters, so the two
    // voices are bit-identical DSP: the rendered blocks must match exactly.
    CHECK(maxDiff == doctest::Approx(0.f));
  }

  // ─── header-variant tolerance ──────────────────────────────────────────────

  TEST_CASE("dx7SysEx: tolerates an arbitrary SysEx channel in the sub-status byte") {
    std::vector<uint8_t> bank = makeBank({fmPatch::brass()}, /*channel*/ 11);
    dx7Bank out;
    REQUIRE(dx7SysEx::parse(bank.data(), bank.size(), out));
    CHECK(out.name(0) == "YSE Brass");
  }

  TEST_CASE("dx7SysEx: tolerates leading and trailing junk around the F0..F7 block") {
    std::vector<uint8_t> bank = makeBank({fmPatch::fm2op()});
    std::vector<uint8_t> padded;
    padded.insert(padded.end(), {0x11, 0x22, 0x33}); // junk before
    padded.insert(padded.end(), bank.begin(), bank.end());
    padded.insert(padded.end(), {0x55, 0x66}); // junk after F7
    dx7Bank out;
    REQUIRE(dx7SysEx::parse(padded.data(), padded.size(), out));
    CHECK(out.size() == 32);
    CHECK(out.name(0) == "YSE 2opFM");
  }

  TEST_CASE("dx7SysEx: accepts a bare unframed 4096-byte packed payload") {
    std::vector<uint8_t> payload(4096);
    packVoice(fmPatch::brass(), payload.data());
    for (int i = 1; i < 32; ++i)
      packVoice(fmPatch::sine(), payload.data() + i * 128);
    dx7Bank out;
    REQUIRE(dx7SysEx::parse(payload.data(), payload.size(), out));
    CHECK(out.size() == 32);
    CHECK(out.name(0) == "YSE Brass");
  }

  // ─── single-voice unpacked dump ───────────────────────────────────────────

  TEST_CASE("dx7SysEx: single-voice unpacked dump round-trips") {
    uint8_t payload[155];
    packUnpackedVoice(fmPatch::fm2op(), payload);
    std::vector<uint8_t> msg = frame(0x00, 0, payload, 155);

    dx7Bank out;
    REQUIRE(dx7SysEx::parse(msg.data(), msg.size(), out));
    REQUIRE(out.size() == 1);
    CHECK(out.name(0) == "YSE 2opFM");

    // Renders identically to the in-code patch.
    const unsigned N = 1024;
    YSE::DSP::buffer ref(N), got(N);
    ref = 0.f;
    got = 0.f;
    render(fmPatch::fm2op(), ref, N);
    render(out.voices[0], got, N);
    float* r = ref.getPtr();
    float* g = got.getPtr();
    float maxDiff = 0.f;
    for (unsigned i = 0; i < N; ++i)
      maxDiff = std::max(maxDiff, std::abs(r[i] - g[i]));
    CHECK(maxDiff == doctest::Approx(0.f));
  }

  TEST_CASE("dx7SysEx: bare 155-byte unpacked payload parses") {
    uint8_t payload[155];
    packUnpackedVoice(fmPatch::brass(), payload);
    dx7Bank out;
    REQUIRE(dx7SysEx::parse(payload, 155, out));
    REQUIRE(out.size() == 1);
    CHECK(out.name(0) == "YSE Brass");
  }

  // ─── corrupt-input robustness ──────────────────────────────────────────────

  TEST_CASE("dx7SysEx: a corrupted payload fails the checksum and is rejected") {
    std::vector<uint8_t> bank = makeBank({fmPatch::fm2op()});
    bank[10] ^= 0x7f; // flip a payload byte; stored checksum no longer matches
    dx7Bank out;
    CHECK_FALSE(dx7SysEx::parse(bank.data(), bank.size(), out));
    CHECK(out.empty()); // output left untouched on failure
  }

  TEST_CASE("dx7SysEx: a corrupted checksum byte is rejected") {
    std::vector<uint8_t> bank = makeBank({fmPatch::brass()});
    bank[bank.size() - 2] ^= 0x01; // the checksum byte sits just before F7
    dx7Bank out;
    CHECK_FALSE(dx7SysEx::parse(bank.data(), bank.size(), out));
    CHECK(out.empty());
  }

  TEST_CASE("dx7SysEx: truncated input is rejected without crashing") {
    std::vector<uint8_t> bank = makeBank({fmPatch::sine()});
    bank.resize(2000); // drop the F7 and tail; length matches no known layout
    dx7Bank out;
    CHECK_FALSE(dx7SysEx::parse(bank.data(), bank.size(), out));
    CHECK(out.empty());
  }

  TEST_CASE("dx7SysEx: empty and null input is rejected") {
    dx7Bank out;
    CHECK_FALSE(dx7SysEx::parse(nullptr, 0, out));
    uint8_t dummy = 0;
    CHECK_FALSE(dx7SysEx::parse(&dummy, 0, out));
    CHECK(out.empty());
  }

  TEST_CASE("dx7SysEx: loadBank on a missing file is rejected without crashing") {
    dx7Bank out;
    CHECK_FALSE(dx7SysEx::loadBank("this/path/does/not/exist.syx", out));
    CHECK(out.empty());
  }

} // TEST_SUITE("dsp")

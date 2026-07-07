/*
  ==============================================================================

    dx7Sysex.cpp
    DX7 SysEx bank importer — implementation. See dx7Sysex.hpp and issue #177.

    The DX7 voice formats are the openly documented Yamaha System-Exclusive
    layouts (``sysex-format.txt``). Two payloads are unpacked here:

      - the 32-voice packed bulk dump (128 packed bytes per voice), and
      - the single-voice unpacked dump (155 bytes, already in the MSFA order).

    Both are mapped into the #176 ``fmPatch`` struct so that
    ``fmPatch::toUnpacked`` reproduces exactly the 156-byte image the ported
    MSFA core (``msfa::Dx7Note::init``) consumes: operator ``op`` at byte
    offset ``op * 21`` with fields [R1..R4 L1..L4 BP LD RD LC RC RS AMS KVS OL
    MODE COARSE FINE DET]. The importer therefore keeps operators in their
    stored positional order — packed/unpacked operator block ``i`` becomes
    ``fmPatch::op[i]`` — which is what MSFA's own unpack does.

  ==============================================================================
*/

#include "dx7Sysex.hpp"

#include <cstring>
#include <fstream>

#include "../../implementations/logImplementation.h"

namespace YSE {
  namespace SYNTH {

    namespace {

      // ─── format constants ────────────────────────────────────────────────
      constexpr size_t kPackedVoiceSize = 128; // one packed voice
      constexpr size_t kPackedBankSize = 4096; // 32 packed voices
      constexpr size_t kPackedBankVoices = 32;
      constexpr size_t kUnpackedVoiceSize = 155; // one DX7 unpacked voice
      constexpr size_t kUnpackedFullSize = 156; // MSFA image (adds op on/off)

      constexpr uint8_t kSysExStart = 0xF0;
      constexpr uint8_t kSysExEnd = 0xF7;
      constexpr uint8_t kYamahaId = 0x43;

      // 7-bit two's-complement checksum over a run of data bytes: the value C
      // for which (sum(bytes) + C) & 0x7f == 0.
      uint8_t sysexChecksum(const uint8_t* p, size_t n) {
        unsigned sum = 0;
        for (size_t i = 0; i < n; ++i)
          sum += p[i];
        return static_cast<uint8_t>((0x80u - (sum & 0x7fu)) & 0x7fu);
      }

      void logError(const std::string& info) {
        INTERNAL::LogImpl().emit(E_FILE_ERROR, "DX7 SysEx: " + info);
      }

      // ─── voice unpackers ─────────────────────────────────────────────────

      // Unpack one 128-byte packed voice into a patch. `src` must hold at least
      // kPackedVoiceSize bytes. Bit-packed fields are split per the DX7 spec.
      void unpackPacked(const uint8_t* src, fmPatch& p) {
        std::memset(&p, 0, sizeof(p));
        for (int op = 0; op < 6; ++op) {
          const uint8_t* s = src + op * 17;
          fmOperator& o = p.op[op];
          for (int i = 0; i < 4; ++i) {
            o.egRate[i] = s[i];
            o.egLevel[i] = s[4 + i];
          }
          o.levelScaleBreakPoint = s[8];
          o.levelScaleLeftDepth = s[9];
          o.levelScaleRightDepth = s[10];
          o.levelScaleLeftCurve = s[11] & 0x03;
          o.levelScaleRightCurve = (s[11] >> 2) & 0x03;
          o.rateScaling = s[12] & 0x07;
          o.detune = (s[12] >> 3) & 0x0f;
          o.ampModSens = s[13] & 0x03;
          o.keyVelSens = (s[13] >> 2) & 0x07;
          o.outputLevel = s[14];
          o.oscMode = s[15] & 0x01;
          o.freqCoarse = (s[15] >> 1) & 0x1f;
          o.freqFine = s[16];
        }
        const uint8_t* g = src + 102; // global block: bytes 102..127
        for (int i = 0; i < 4; ++i) {
          p.pitchEgRate[i] = g[i];
          p.pitchEgLevel[i] = g[4 + i];
        }
        p.algorithm = g[8] & 0x1f;
        p.feedback = g[9] & 0x07;
        p.oscKeySync = (g[9] >> 3) & 0x01;
        p.lfoSpeed = g[10];
        p.lfoDelay = g[11];
        p.lfoPitchModDepth = g[12];
        p.lfoAmpModDepth = g[13];
        p.lfoSync = g[14] & 0x01;
        p.lfoWaveform = (g[14] >> 1) & 0x07;
        p.pitchModSens = (g[14] >> 4) & 0x07;
        p.transpose = g[15];
        for (int i = 0; i < 10; ++i)
          p.name[i] = static_cast<char>(g[16 + i]);
        p.opEnabled = 0x3f; // not carried in the DX7 voice; default all on
      }

      // Unpack one unpacked-format voice (155 or 156 bytes) into a patch. The
      // unpacked byte order already matches the MSFA image, so this is a
      // field-for-field copy with defensive masking on the bit-width fields.
      void unpackUnpacked(const uint8_t* v, fmPatch& p) {
        std::memset(&p, 0, sizeof(p));
        for (int op = 0; op < 6; ++op) {
          const uint8_t* s = v + op * 21;
          fmOperator& o = p.op[op];
          for (int i = 0; i < 4; ++i) {
            o.egRate[i] = s[i];
            o.egLevel[i] = s[4 + i];
          }
          o.levelScaleBreakPoint = s[8];
          o.levelScaleLeftDepth = s[9];
          o.levelScaleRightDepth = s[10];
          o.levelScaleLeftCurve = s[11] & 0x03;
          o.levelScaleRightCurve = s[12] & 0x03;
          o.rateScaling = s[13] & 0x07;
          o.ampModSens = s[14] & 0x03;
          o.keyVelSens = s[15] & 0x07;
          o.outputLevel = s[16];
          o.oscMode = s[17] & 0x01;
          o.freqCoarse = s[18] & 0x1f;
          o.freqFine = s[19];
          o.detune = s[20] & 0x0f;
        }
        for (int i = 0; i < 4; ++i) {
          p.pitchEgRate[i] = v[126 + i];
          p.pitchEgLevel[i] = v[130 + i];
        }
        p.algorithm = v[134] & 0x1f;
        p.feedback = v[135] & 0x07;
        p.oscKeySync = v[136] & 0x01;
        p.lfoSpeed = v[137];
        p.lfoDelay = v[138];
        p.lfoPitchModDepth = v[139];
        p.lfoAmpModDepth = v[140];
        p.lfoSync = v[141] & 0x01;
        p.lfoWaveform = v[142] & 0x07;
        p.pitchModSens = v[143] & 0x07;
        p.transpose = v[144];
        for (int i = 0; i < 10; ++i)
          p.name[i] = static_cast<char>(v[145 + i]);
        p.opEnabled = 0x3f;
      }

      // Fill `out` from a raw (unframed) payload identified purely by length.
      // No checksum is available for raw payloads. Returns false if the length
      // matches no known layout.
      bool parseRawPayload(const uint8_t* data, size_t length, dx7Bank& out) {
        std::vector<fmPatch> voices;
        if (length == kPackedBankSize) {
          voices.resize(kPackedBankVoices);
          for (size_t i = 0; i < kPackedBankVoices; ++i)
            unpackPacked(data + i * kPackedVoiceSize, voices[i]);
        } else if (length == kPackedVoiceSize) {
          voices.resize(1);
          unpackPacked(data, voices[0]);
        } else if (length == kUnpackedVoiceSize || length == kUnpackedFullSize) {
          voices.resize(1);
          unpackUnpacked(data, voices[0]);
        } else {
          logError("unrecognised payload length (" + std::to_string(length) + " bytes)");
          return false;
        }
        out.voices = std::move(voices);
        return true;
      }

    } // namespace

    // ─── dx7Bank helpers ─────────────────────────────────────────────────────

    std::string dx7Bank::name(size_t index) const {
      if (index >= voices.size()) return std::string();
      const char* n = voices[index].name;
      size_t len = 10; // DX7 names are 10 chars, space-padded, not terminated
      while (len > 0 && (n[len - 1] == ' ' || n[len - 1] == '\0'))
        --len;
      return std::string(n, n + len);
    }

    int dx7Bank::indexOf(const char* voiceName) const {
      if (voiceName == nullptr) return -1;
      for (size_t i = 0; i < voices.size(); ++i)
        if (name(i) == voiceName) return static_cast<int>(i);
      return -1;
    }

    // ─── dx7SysEx ────────────────────────────────────────────────────────────

    bool dx7SysEx::parse(const uint8_t* data, size_t length, dx7Bank& out) {
      if (data == nullptr || length == 0) {
        logError("empty input");
        return false;
      }

      // 1. Locate an F0 … F7 SysEx block (tolerating leading/trailing junk).
      size_t start = length;
      for (size_t i = 0; i < length; ++i) {
        if (data[i] == kSysExStart) {
          start = i;
          break;
        }
      }
      if (start != length && start + 1 < length && data[start + 1] == kYamahaId) {
        size_t end = length;
        for (size_t j = start + 1; j < length; ++j) {
          if (data[j] == kSysExEnd) {
            end = j;
            break;
          }
        }
        if (end != length) {
          // A framed Yamaha message: [F0][43][sub/chan][fmt][cMS][cLS][payload…][cksum][F7].
          // Trust the actual framing length over the declared count so common
          // header variants (any channel, off-by-one count) still parse.
          const size_t msgLen = end - start + 1;
          if (msgLen < 8) {
            logError("truncated SysEx header");
            return false;
          }
          const uint8_t* payload = data + start + 6;
          const size_t payloadLen = msgLen - 8; // minus F0,43,sub,fmt,cMS,cLS,cksum,F7
          const uint8_t declaredCk = data[end - 1];
          if (sysexChecksum(payload, payloadLen) != declaredCk) {
            logError("checksum mismatch");
            return false;
          }
          std::vector<fmPatch> voices;
          if (payloadLen == kUnpackedVoiceSize) {
            voices.resize(1);
            unpackUnpacked(payload, voices[0]);
          } else if (payloadLen == kPackedVoiceSize) {
            voices.resize(1);
            unpackPacked(payload, voices[0]);
          } else if (payloadLen == kPackedBankSize) {
            voices.resize(kPackedBankVoices);
            for (size_t i = 0; i < kPackedBankVoices; ++i)
              unpackPacked(payload + i * kPackedVoiceSize, voices[i]);
          } else {
            logError("unrecognised SysEx payload length (" + std::to_string(payloadLen) +
                     " bytes)");
            return false;
          }
          out.voices = std::move(voices);
          return true;
        }
      }

      // 2. No usable frame — treat the input as a raw payload keyed by length.
      return parseRawPayload(data, length, out);
    }

    bool dx7SysEx::loadBank(const char* path, dx7Bank& out) {
      if (path == nullptr) {
        logError("null path");
        return false;
      }
      std::ifstream in(path, std::ios::binary | std::ios::ate);
      if (!in.is_open()) {
        logError(std::string("cannot open file: ") + path);
        return false;
      }
      const std::streamsize size = in.tellg();
      if (size <= 0) {
        logError(std::string("empty file: ") + path);
        return false;
      }
      in.seekg(0, std::ios::beg);
      std::vector<uint8_t> bytes(static_cast<size_t>(size));
      if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) {
        logError(std::string("read error: ") + path);
        return false;
      }
      return parse(bytes.data(), bytes.size(), out);
    }

  } // namespace SYNTH
} // namespace YSE

/*
  ==============================================================================

    sfzModel.cpp
    SFZ v1 opcode-subset parser + region model (issue #173).
    Contract: docs/design/sfz_opcode_subset.md.

  ==============================================================================
*/

#include "sfzModel.hpp"

#include "../implementations/logImplementation.h"
#include "../utils/fileFunctions.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <numeric>
#include <sstream>

namespace YSE {
  namespace DSP {

    namespace {

      // ---- small string helpers -------------------------------------------

      std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
      }

      // Log an SFZ parse note through the engine log facility. All parsing is
      // offline (slow pool), so this never runs on the audio thread.
      void logSFZ(ERROR_CODE code, const std::string& source, const std::string& detail) {
        std::string msg = "[sfz";
        if (!source.empty()) msg += " " + source;
        msg += "] " + detail;
        INTERNAL::LogImpl().emit(code, msg);
      }

      // Normalise backslashes to forward slashes (SFZ files from Windows tools
      // use '\'). Forward slashes work on every platform we target.
      std::string normaliseSeparators(std::string p) {
        std::replace(p.begin(), p.end(), '\\', '/');
        return p;
      }

      // Strip SFZ comments: line comments ("// ... EOL") and C-style block
      // comments ("/* ... */", possibly multi-line). Done before tokenising.
      std::string stripComments(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        bool inBlock = false;
        for (size_t i = 0; i < in.size(); ++i) {
          if (inBlock) {
            if (in[i] == '*' && i + 1 < in.size() && in[i + 1] == '/') {
              inBlock = false;
              ++i;
            }
            continue;
          }
          if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '*') {
            inBlock = true;
            ++i;
            continue;
          }
          if (in[i] == '/' && i + 1 < in.size() && in[i + 1] == '/') {
            // line comment: skip to end of line (keep the newline)
            while (i < in.size() && in[i] != '\n')
              ++i;
            if (i < in.size()) out += '\n';
            continue;
          }
          out += in[i];
        }
        return out;
      }

      // ---- value parsing --------------------------------------------------

      // Parse an integer, tolerating a leading/trailing sign and stray space.
      bool parseIntValue(const std::string& v, int& out) {
        if (v.empty()) return false;
        char* end = nullptr;
        long n = std::strtol(v.c_str(), &end, 10);
        if (end == v.c_str()) return false;
        while (*end == ' ' || *end == '\t')
          ++end;
        if (*end != '\0') return false;
        out = static_cast<int>(n);
        return true;
      }

      bool parseLongValue(const std::string& v, long& out) {
        if (v.empty()) return false;
        char* end = nullptr;
        long n = std::strtol(v.c_str(), &end, 10);
        if (end == v.c_str()) return false;
        while (*end == ' ' || *end == '\t')
          ++end;
        if (*end != '\0') return false;
        out = n;
        return true;
      }

      bool parseFloatValue(const std::string& v, float& out) {
        if (v.empty()) return false;
        char* end = nullptr;
        double n = std::strtod(v.c_str(), &end);
        if (end == v.c_str()) return false;
        while (*end == ' ' || *end == '\t')
          ++end;
        if (*end != '\0') return false;
        out = static_cast<float>(n);
        return true;
      }

      // Note name -> MIDI number, middle-C convention c4 = 60 (SFZ/sfizz
      // default). Accepts plain integers, or note names like c4, f#3, bb2,
      // c-1. Returns false on anything unrecognised.
      bool parseKeyValue(const std::string& raw, int& out) {
        std::string v = raw;
        // trim
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t'))
          v.erase(v.begin());
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
          v.pop_back();
        if (v.empty()) return false;

        // plain integer?
        int asInt = 0;
        if (parseIntValue(v, asInt)) {
          out = asInt;
          return true;
        }

        std::string low = toLower(v);
        size_t idx = 0;
        int semitone;
        switch (low[idx]) {
        case 'c':
          semitone = 0;
          break;
        case 'd':
          semitone = 2;
          break;
        case 'e':
          semitone = 4;
          break;
        case 'f':
          semitone = 5;
          break;
        case 'g':
          semitone = 7;
          break;
        case 'a':
          semitone = 9;
          break;
        case 'b':
          semitone = 11;
          break;
        default:
          return false;
        }
        ++idx;
        // accidental
        if (idx < low.size() && (low[idx] == '#' || low[idx] == 's')) {
          semitone += 1;
          ++idx;
        } else if (idx < low.size() && low[idx] == 'b') {
          semitone -= 1;
          ++idx;
        }
        if (idx >= low.size()) return false; // needs an octave
        int octave = 0;
        if (!parseIntValue(low.substr(idx), octave)) return false;
        int note = (octave + 1) * 12 + semitone;
        if (note < 0 || note > 127) return false;
        out = note;
        return true;
      }

      bool parseLoopMode(const std::string& v, int& out) {
        std::string low = toLower(v);
        if (low == "no_loop") {
          out = SFZ_NO_LOOP;
          return true;
        }
        if (low == "one_shot") {
          out = SFZ_ONE_SHOT;
          return true;
        }
        if (low == "loop_continuous") {
          out = SFZ_LOOP_CONTINUOUS;
          return true;
        }
        if (low == "loop_sustain") {
          out = SFZ_LOOP_SUSTAIN;
          return true;
        }
        return false;
      }

      bool parseOffMode(const std::string& v, int& out) {
        std::string low = toLower(v);
        if (low == "fast") {
          out = SFZ_OFF_FAST;
          return true;
        }
        if (low == "normal") {
          out = SFZ_OFF_NORMAL;
          return true;
        }
        return false;
      }

      bool parseCurve(const std::string& v, int& out) {
        std::string low = toLower(v);
        if (low == "gain") {
          out = SFZ_CURVE_GAIN;
          return true;
        }
        if (low == "power") {
          out = SFZ_CURVE_POWER;
          return true;
        }
        return false;
      }

      // The set of opcodes we recognise (IN, per spec §4). Anything outside
      // this set is skipped-and-logged rather than failing the parse.
      bool isKnownOpcode(const std::string& op) {
        static const char* const known[] = {
            "sample",       "default_path",    "lokey",         "hikey",
            "key",          "pitch_keycenter", "lovel",         "hivel",
            "tune",         "pitch",           "transpose",     "pitch_keytrack",
            "loop_mode",    "loop_start",      "loop_end",      "offset",
            "end",          "volume",          "pan",           "amp_veltrack",
            "xfin_lovel",   "xfin_hivel",      "xfout_lovel",   "xfout_hivel",
            "xfin_lokey",   "xfin_hikey",      "xfout_lokey",   "xfout_hikey",
            "xf_velcurve",  "xf_keycurve",     "ampeg_delay",   "ampeg_attack",
            "ampeg_hold",   "ampeg_decay",     "ampeg_sustain", "ampeg_release",
            "group",        "polyphony_group", "off_by",        "off_mode",
            "seq_position", "seq_length",
        };
        for (const char* k : known) {
          if (op == k) return true;
        }
        return false;
      }

      // ---- opcode dictionaries + merge ------------------------------------

      using OpcodeMap = std::map<std::string, std::string>;

      // Innermost wins: global <- master <- group <- region-own.
      void mergeInto(OpcodeMap& dst, const OpcodeMap& src) {
        for (const auto& kv : src)
          dst[kv.first] = kv.second;
      }

    } // namespace

    // -----------------------------------------------------------------------
    // Parser
    // -----------------------------------------------------------------------

    // A running builder that flattens the four-level header hierarchy into a
    // region table as regions are encountered.
    namespace {

      struct Parser {
        sfzInstrument inst;
        std::string source;
        std::string sfzDir;

        OpcodeMap globalOps;
        OpcodeMap masterOps;
        OpcodeMap groupOps;
        std::string defaultPath; // running, updated wherever default_path appears

        // De-dup sample table. Keyed by the resolved lookup string.
        std::map<std::string, int> sampleIndexByKey;

        // Resolve a sample= value to a table index, or -1 to drop the region.
        int resolveSample(const std::string& rawValue) {
          std::string value = rawValue;
          // trim
          while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(value.begin());
          while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.pop_back();

          if (value.empty()) return -1;

          if (toLower(value) == "*silence") {
            auto it = sampleIndexByKey.find("*silence");
            if (it != sampleIndexByKey.end()) return it->second;
            sfzSample s;
            s.silence = true;
            int idx = static_cast<int>(inst.samples.size());
            inst.samples.push_back(s);
            sampleIndexByKey["*silence"] = idx;
            return idx;
          }

          std::string rel = normaliseSeparators(value);
          std::string resolved;
          if (IsPathAbsolute(rel)) {
            resolved = rel;
          } else {
            std::string prefix = normaliseSeparators(defaultPath);
            std::string base = sfzDir;
            if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
            if (!prefix.empty() && prefix.back() != '/') prefix += '/';
            resolved = base + prefix + rel;
          }

          if (!FileExists(resolved)) {
            logSFZ(E_FILE_NOT_FOUND, source, "sample not found, region dropped: " + resolved);
            return -1;
          }

          auto it = sampleIndexByKey.find(resolved);
          if (it != sampleIndexByKey.end()) return it->second;
          sfzSample s;
          s.path = resolved;
          int idx = static_cast<int>(inst.samples.size());
          inst.samples.push_back(s);
          sampleIndexByKey[resolved] = idx;
          return idx;
        }

        // Apply one opcode to a region under construction. Malformed known
        // values are logged as warnings and the default is kept.
        void applyOpcode(sfzRegion& r, const std::string& op, const std::string& value) {
          int i = 0;
          long l = 0;
          float f = 0.0f;

          auto warnBad = [&]() {
            logSFZ(E_WARNING, source, "malformed value for '" + op + "': '" + value + "'");
          };

          if (op == "lokey") {
            if (parseKeyValue(value, i))
              r.lokey = i;
            else
              warnBad();
          } else if (op == "hikey") {
            if (parseKeyValue(value, i))
              r.hikey = i;
            else
              warnBad();
          } else if (op == "key") {
            if (parseKeyValue(value, i)) {
              r.lokey = r.hikey = r.pitchKeycenter = i;
            } else
              warnBad();
          } else if (op == "pitch_keycenter") {
            if (parseKeyValue(value, i))
              r.pitchKeycenter = i;
            else
              warnBad();
          } else if (op == "lovel") {
            if (parseIntValue(value, i))
              r.lovel = i;
            else
              warnBad();
          } else if (op == "hivel") {
            if (parseIntValue(value, i))
              r.hivel = i;
            else
              warnBad();
          } else if (op == "tune" || op == "pitch") {
            if (parseFloatValue(value, f))
              r.tuneCents = f;
            else
              warnBad();
          } else if (op == "transpose") {
            if (parseIntValue(value, i))
              r.transposeSemis = i;
            else
              warnBad();
          } else if (op == "pitch_keytrack") {
            if (parseFloatValue(value, f))
              r.pitchKeytrack = f;
            else
              warnBad();
          } else if (op == "loop_mode") {
            if (parseLoopMode(value, i))
              r.loopMode = i;
            else
              warnBad();
          } else if (op == "loop_start") {
            if (parseLongValue(value, l))
              r.loopStart = l;
            else
              warnBad();
          } else if (op == "loop_end") {
            if (parseLongValue(value, l))
              r.loopEnd = l;
            else
              warnBad();
          } else if (op == "offset") {
            if (parseLongValue(value, l))
              r.offset = l;
            else
              warnBad();
          } else if (op == "end") {
            if (parseLongValue(value, l))
              r.endFrame = l;
            else
              warnBad();
          } else if (op == "volume") {
            if (parseFloatValue(value, f))
              r.volumeDb = f;
            else
              warnBad();
          } else if (op == "pan") {
            if (parseFloatValue(value, f))
              r.pan = f;
            else
              warnBad();
          } else if (op == "amp_veltrack") {
            if (parseFloatValue(value, f))
              r.ampVeltrack = f;
            else
              warnBad();
          } else if (op == "xfin_lovel") {
            if (parseIntValue(value, i))
              r.xfinLovel = i;
            else
              warnBad();
          } else if (op == "xfin_hivel") {
            if (parseIntValue(value, i))
              r.xfinHivel = i;
            else
              warnBad();
          } else if (op == "xfout_lovel") {
            if (parseIntValue(value, i))
              r.xfoutLovel = i;
            else
              warnBad();
          } else if (op == "xfout_hivel") {
            if (parseIntValue(value, i))
              r.xfoutHivel = i;
            else
              warnBad();
          } else if (op == "xfin_lokey") {
            if (parseKeyValue(value, i))
              r.xfinLokey = i;
            else
              warnBad();
          } else if (op == "xfin_hikey") {
            if (parseKeyValue(value, i))
              r.xfinHikey = i;
            else
              warnBad();
          } else if (op == "xfout_lokey") {
            if (parseKeyValue(value, i))
              r.xfoutLokey = i;
            else
              warnBad();
          } else if (op == "xfout_hikey") {
            if (parseKeyValue(value, i))
              r.xfoutHikey = i;
            else
              warnBad();
          } else if (op == "xf_velcurve") {
            if (parseCurve(value, i))
              r.xfVelcurve = i;
            else
              warnBad();
          } else if (op == "xf_keycurve") {
            if (parseCurve(value, i))
              r.xfKeycurve = i;
            else
              warnBad();
          } else if (op == "ampeg_delay") {
            if (parseFloatValue(value, f))
              r.egDelay = f;
            else
              warnBad();
          } else if (op == "ampeg_attack") {
            if (parseFloatValue(value, f))
              r.egAttack = f;
            else
              warnBad();
          } else if (op == "ampeg_hold") {
            if (parseFloatValue(value, f))
              r.egHold = f;
            else
              warnBad();
          } else if (op == "ampeg_decay") {
            if (parseFloatValue(value, f))
              r.egDecay = f;
            else
              warnBad();
          } else if (op == "ampeg_sustain") {
            if (parseFloatValue(value, f))
              r.egSustain = f / 100.0f;
            else
              warnBad();
          } else if (op == "ampeg_release") {
            if (parseFloatValue(value, f))
              r.egRelease = f;
            else
              warnBad();
          } else if (op == "group" || op == "polyphony_group") {
            if (parseIntValue(value, i))
              r.chokeGroup = i;
            else
              warnBad();
          } else if (op == "off_by") {
            if (parseIntValue(value, i))
              r.offBy = i;
            else
              warnBad();
          } else if (op == "off_mode") {
            if (parseOffMode(value, i))
              r.offMode = i;
            else
              warnBad();
          } else if (op == "seq_position") {
            if (parseIntValue(value, i))
              r.seqPosition = i;
            else
              warnBad();
          } else if (op == "seq_length") {
            if (parseIntValue(value, i))
              r.seqLength = i;
            else
              warnBad();
          }
          // sample / default_path handled by the caller; unknown handled at
          // tokenise time.
        }

        // Finalise a <region>: merge the hierarchy, resolve the sample, apply
        // opcodes, and append to the table (or drop it).
        void finaliseRegion(const OpcodeMap& regionOps) {
          OpcodeMap eff = globalOps;
          mergeInto(eff, masterOps);
          mergeInto(eff, groupOps);
          mergeInto(eff, regionOps);

          // sample is mandatory for a playable region.
          auto sampleIt = eff.find("sample");
          if (sampleIt == eff.end()) {
            logSFZ(E_WARNING, source, "region with no sample opcode dropped");
            ++inst.droppedRegions;
            return;
          }

          // end = -1 explicitly disables the region (SFZ convention) -> drop
          // it. This is distinct from the endFrame default (SFZ_UNSET = "play
          // to the file end"), which must NOT drop the region.
          auto endIt = eff.find("end");
          if (endIt != eff.end()) {
            long ev = 0;
            if (parseLongValue(endIt->second, ev) && ev == -1) {
              ++inst.droppedRegions;
              return;
            }
          }

          int sampleIdx = resolveSample(sampleIt->second);
          if (sampleIdx < 0) {
            ++inst.droppedRegions;
            return;
          }

          sfzRegion r;
          r.sampleIndex = sampleIdx;
          for (const auto& kv : eff) {
            if (kv.first == "sample" || kv.first == "default_path") continue;
            applyOpcode(r, kv.first, kv.second);
          }

          // Defensive clamps so an inverted/absurd range never breaks the
          // audio-thread matcher; log but keep the region.
          if (r.seqLength < 1) r.seqLength = 1;
          if (r.seqPosition < 1) r.seqPosition = 1;

          inst.regions.push_back(r);
        }
      };

      // Tokenise comment-stripped SFZ into a header/opcode stream and drive the
      // Parser. Handles values containing spaces (sample paths) by consuming
      // continuation tokens up to the next opcode/header.
      void run(Parser& P, const std::string& text) {
        std::string clean = stripComments(text);

        // Split into whitespace-separated tokens (newlines are not significant
        // once comments are gone).
        std::vector<std::string> tokens;
        {
          std::string cur;
          for (char c : clean) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
              if (!cur.empty()) {
                tokens.push_back(cur);
                cur.clear();
              }
            } else {
              cur += c;
            }
          }
          if (!cur.empty()) tokens.push_back(cur);
        }

        auto isHeader = [](const std::string& t) {
          return t.size() >= 2 && t.front() == '<' && t.back() == '>';
        };

        enum Scope { S_NONE, S_GLOBAL, S_MASTER, S_GROUP, S_REGION, S_CONTROL, S_SKIP };
        Scope scope = S_NONE;
        OpcodeMap regionOps;
        bool regionOpen = false;

        auto closeRegion = [&]() {
          if (regionOpen) {
            P.finaliseRegion(regionOps);
            regionOps.clear();
            regionOpen = false;
          }
        };

        for (size_t t = 0; t < tokens.size(); ++t) {
          const std::string& tok = tokens[t];

          if (isHeader(tok)) {
            closeRegion();
            std::string name = toLower(tok.substr(1, tok.size() - 2));
            if (name == "global") {
              scope = S_GLOBAL;
              P.globalOps.clear();
              P.masterOps.clear();
              P.groupOps.clear();
            } else if (name == "master") {
              scope = S_MASTER;
              P.masterOps.clear();
              P.groupOps.clear();
            } else if (name == "group") {
              scope = S_GROUP;
              P.groupOps.clear();
            } else if (name == "region") {
              scope = S_REGION;
              regionOpen = true;
            } else if (name == "control") {
              scope = S_CONTROL;
            } else {
              logSFZ(E_DEBUG, P.source, "unsupported header <" + name + "> skipped");
              scope = S_SKIP;
            }
            continue;
          }

          // opcode=value (value may continue across following tokens)
          size_t eq = tok.find('=');
          if (eq == std::string::npos) {
            logSFZ(E_DEBUG, P.source, "stray token skipped: " + tok);
            continue;
          }
          std::string op = toLower(tok.substr(0, eq));
          std::string value = tok.substr(eq + 1);
          // consume continuation tokens (spaces in a value, e.g. sample paths)
          while (t + 1 < tokens.size() && !isHeader(tokens[t + 1]) &&
                 tokens[t + 1].find('=') == std::string::npos) {
            value += " " + tokens[t + 1];
            ++t;
          }

          // default_path is tolerated in any header and updates the running value.
          if (op == "default_path") {
            P.defaultPath = value;
            P.inst.defaultPath = value;
            continue;
          }

          if (scope == S_SKIP) continue;

          if (!isKnownOpcode(op)) {
            logSFZ(E_DEBUG, P.source, "unknown/unsupported opcode skipped: " + op);
            continue;
          }

          switch (scope) {
          case S_GLOBAL:
            P.globalOps[op] = value;
            break;
          case S_MASTER:
            P.masterOps[op] = value;
            break;
          case S_GROUP:
            P.groupOps[op] = value;
            break;
          case S_REGION:
            regionOps[op] = value;
            break;
          case S_CONTROL: // only default_path (handled above) is read here
          case S_NONE:
          default:
            logSFZ(E_DEBUG, P.source, "opcode outside a region context skipped: " + op);
            break;
          }
        }
        closeRegion();
      }

      // Parse-time overflow warning (spec §5): flag when any (note, velocity)
      // cell can exceed the layer bound on some round-robin hit. Offline scan;
      // allocation is fine here (never the audio thread).
      void computeLayerOverflow(sfzInstrument& inst) {
        if (inst.regions.size() <= static_cast<size_t>(YSE_MAX_REGION_LAYERS)) return;

        for (int note = 0; note <= 127 && !inst.layerOverflowWarning; ++note) {
          for (int vel = 1; vel <= 127 && !inst.layerOverflowWarning; ++vel) {
            // Collect matching regions for this cell.
            std::vector<const sfzRegion*> matched;
            for (const auto& r : inst.regions) {
              if (regionMatches(r, note, vel)) matched.push_back(&r);
            }
            if (matched.size() <= static_cast<size_t>(YSE_MAX_REGION_LAYERS)) continue;

            // Round-robin variants are alternatives, not simultaneous. Walk one
            // full counter cycle (lcm of seq lengths, capped) and take the max
            // simultaneous count.
            long cycle = 1;
            for (const sfzRegion* r : matched) {
              long L = r->seqLength > 0 ? r->seqLength : 1;
              cycle = std::lcm(cycle, L);
              if (cycle > 512) {
                cycle = 512;
                break;
              }
            }
            for (long hit = 0; hit < cycle && !inst.layerOverflowWarning; ++hit) {
              int simultaneous = 0;
              for (const sfzRegion* r : matched) {
                long L = r->seqLength > 0 ? r->seqLength : 1;
                if ((hit % L) == (r->seqPosition - 1)) ++simultaneous;
              }
              if (simultaneous > YSE_MAX_REGION_LAYERS) inst.layerOverflowWarning = true;
            }
          }
        }

        if (inst.layerOverflowWarning) {
          logSFZ(E_WARNING, inst.sourceFile,
                 "a (note,velocity) cell can exceed " + std::to_string(YSE_MAX_REGION_LAYERS) +
                     " simultaneous layers; extra layers will be truncated by priority");
        }
      }

    } // namespace

    sfzInstrument parseSFZ(const std::string& text, const std::string& sfzDir,
                           const std::string& sourceName) {
      Parser P;
      P.source = sourceName;
      P.sfzDir = sfzDir;
      P.inst.sourceFile = sourceName;

      run(P, text);

      P.inst.valid = !P.inst.regions.empty();
      if (!P.inst.valid) {
        logSFZ(E_FILE_ERROR, sourceName, "no valid regions; instrument failed to load");
      }
      computeLayerOverflow(P.inst);
      return P.inst;
    }

    sfzInstrument loadSFZ(const std::string& filePath) {
      std::ifstream file(filePath, std::ios::in | std::ios::binary);
      if (!file.good()) {
        sfzInstrument inst;
        inst.sourceFile = filePath;
        inst.valid = false;
        logSFZ(E_FILE_ERROR, filePath, "cannot open .sfz file");
        return inst;
      }
      std::stringstream ss;
      ss << file.rdbuf();

      // Derive the .sfz directory for relative sample resolution.
      std::string dir;
      std::string norm = normaliseSeparators(filePath);
      size_t slash = norm.find_last_of('/');
      if (slash != std::string::npos) dir = norm.substr(0, slash);

      return parseSFZ(ss.str(), dir, filePath);
    }

    // -----------------------------------------------------------------------
    // Region resolution (audio-thread callable, allocation-free)
    // -----------------------------------------------------------------------

    bool regionMatches(const sfzRegion& r, int note, int velocity) {
      return note >= r.lokey && note <= r.hikey && velocity >= r.lovel && velocity <= r.hivel;
    }

    namespace {
      // Priority comparator for layer-limit truncation (spec §5): higher
      // priority = narrowest velocity range, then narrowest key range, then
      // last-in-file (higher index). Returns true if region `a` outranks `b`.
      bool higherPriority(const sfzInstrument& inst, int a, int b) {
        const sfzRegion& ra = inst.regions[a];
        const sfzRegion& rb = inst.regions[b];
        int velA = ra.hivel - ra.lovel;
        int velB = rb.hivel - rb.lovel;
        if (velA != velB) return velA < velB;
        int keyA = ra.hikey - ra.lokey;
        int keyB = rb.hikey - rb.lokey;
        if (keyA != keyB) return keyA < keyB;
        return a > b; // later in file wins
      }
    } // namespace

    int resolveLayers(const sfzInstrument& inst, int note, int velocity, int hit,
                      int outIndices[YSE_MAX_REGION_LAYERS]) {
      // Fixed-size priority buffer kept sorted best-first; no allocation.
      int best[YSE_MAX_REGION_LAYERS];
      int count = 0;

      const int n = static_cast<int>(inst.regions.size());
      for (int i = 0; i < n; ++i) {
        const sfzRegion& r = inst.regions[i];
        if (!regionMatches(r, note, velocity)) continue;
        // Round-robin filter: region sounds only on its slot of the cycle.
        int L = r.seqLength > 0 ? r.seqLength : 1;
        if ((hit % L) != (r.seqPosition - 1)) continue;

        if (count < YSE_MAX_REGION_LAYERS) {
          // insert keeping best[] sorted by descending priority
          int pos = count;
          while (pos > 0 && higherPriority(inst, i, best[pos - 1])) {
            best[pos] = best[pos - 1];
            --pos;
          }
          best[pos] = i;
          ++count;
        } else if (higherPriority(inst, i, best[count - 1])) {
          // replace the weakest, then bubble into place
          int pos = count - 1;
          while (pos > 0 && higherPriority(inst, i, best[pos - 1])) {
            best[pos] = best[pos - 1];
            --pos;
          }
          best[pos] = i;
        }
      }

      // Return in ascending file order for a stable, set-comparable result.
      for (int a = 0; a < count; ++a)
        outIndices[a] = best[a];
      std::sort(outIndices, outIndices + count);
      return count;
    }

  } // namespace DSP
} // namespace YSE

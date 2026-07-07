/*
  ==============================================================================

    dx7Sysex.hpp
    DX7 SysEx bank importer for YSE::synth (issue #177).

    Parses the openly documented Yamaha DX7 System-Exclusive voice formats and
    unpacks each voice into the #176 ``fmPatch`` struct (the explicit data
    contract between the FM engine and this importer). Two formats are handled:

      - the 32-voice packed bulk dump (header ``F0 43 0n 09 20 00`` + 4096
        payload bytes + checksum + ``F7``; 128 packed bytes per voice), and
      - the single-voice unpacked dump (header ``F0 43 0n 00 01 1B`` + 155
        payload bytes + checksum + ``F7``).

    The parser is tolerant of the header variants found in the wild: any
    SysEx channel in the sub-status byte, leading/trailing junk around the
    ``F0 … F7`` block, and unwrapped raw payloads (a bare 4096-byte packed
    bank, a bare 128-byte packed voice, or a bare 155-byte unpacked voice).
    A present checksum is validated and a mismatch rejects the input.

    Offline only — call from the setup / slow-pool thread, never from the audio
    callback (it reads files and allocates). On failure it logs
    ``E_FILE_ERROR`` and returns false, leaving the output untouched.

    Non-goals (see issue #177): SysEx *output* / device dumps, DX7II/TX802
    extended formats, and the instrument C-API (that is #178).

  ==============================================================================
*/

#ifndef YSE_DSP_FM_DX7SYSEX_HPP
#define YSE_DSP_FM_DX7SYSEX_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fmPatch.hpp"
#include "../../headers/defines.hpp"

namespace YSE {
  namespace SYNTH {

    /**
     *  @brief A parsed DX7 bank: the voices plus name lookup helpers.
     *
     *  A packed bulk dump yields 32 voices; a single-voice dump yields one.
     *  Plain data — safe to copy and hand across the setup boundary.
     */
    struct API dx7Bank {
      std::vector<fmPatch> voices; ///< Parsed voices, in bank order.

      /// @brief Number of voices in the bank.
      size_t size() const {
        return voices.size();
      }
      /// @brief True when no voice was parsed.
      bool empty() const {
        return voices.empty();
      }

      /**
       *  @brief Voice name at @p index, trimmed of trailing spaces.
       *  @return The name, or an empty string if @p index is out of range.
       */
      std::string name(size_t index) const;

      /**
       *  @brief First voice whose (trimmed) name equals @p voiceName.
       *  @return The voice index, or -1 if no voice matches.
       */
      int indexOf(const char* voiceName) const;
    };

    /**
     *  @brief Stateless DX7 SysEx parser. Offline / setup-thread only.
     */
    class API dx7SysEx {
    public:
      /**
       *  @brief Parse a DX7 SysEx image from memory.
       *  @param data   Raw bytes (a ``.syx`` file image or an in-memory dump).
       *  @param length Number of bytes at @p data.
       *  @param out    Filled with the parsed voices on success; untouched on
       *                failure.
       *  @return true on success. On failure logs ``E_FILE_ERROR`` and returns
       *          false (bad header, wrong length, or checksum mismatch).
       */
      static bool parse(const uint8_t* data, size_t length, dx7Bank& out);

      /**
       *  @brief Load and parse a DX7 ``.syx`` file.
       *  @param path UTF-8 path to the SysEx file.
       *  @param out  Filled with the parsed voices on success.
       *  @return true on success; on failure logs ``E_FILE_ERROR`` (file not
       *          found / unreadable, or a parse error) and returns false.
       */
      static bool loadBank(const char* path, dx7Bank& out);
    };

  } // namespace SYNTH
} // namespace YSE

#endif // YSE_DSP_FM_DX7SYSEX_HPP

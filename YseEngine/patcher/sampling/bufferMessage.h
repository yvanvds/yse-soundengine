#pragma once

#include <cstddef>
#include <cstdint>
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    /** @brief What a ``~buffer`` message does to its buffer (issue #1039). */
    enum class BUFFER_OP : std::uint8_t {
      READ,
      WRITE,
      FILL,
      FILL_SINE,
      WINDOW,
      GAIN,
      OFFSET,
      DELTAS,
      NORMALIZE,
      SIZE_MS,
      SIZE_SAMPLES,
      CROP_MS,
      CROP_SAMPLES,
      DUPLICATE,
      RATE,
      QUERY,
    };

    /** @brief Which shape a ``~buffer`` query asks for. */
    enum class BUFFER_QUERY : std::uint8_t {
      SAMPS,
      SIZE,
      SR,
      CHANS,
    };

    /**
     *  @brief One ``~buffer`` message as a fixed record, so it can travel to
     *         the background pool in a ``fileScheduler`` slot (issue #1039).
     *
     *  ``~buffer`` sends one per message; ``~polybuffer``'s ``send`` carries
     *  one inside its own record and applies it to each buffer it names
     *  (issue #1139). Trivially copyable: a slot copies it with ``memcpy``.
     *  ``ApplyBufferMessage`` writes the buffer's shape after the edit into
     *  the reply fields.
     */
    struct bufferMessage {
      std::uint64_t id = 0; // the buffer, for a sender that finds it by id
      BUFFER_OP op = BUFFER_OP::QUERY;
      std::uint8_t flag = 0; // replace / cosine / half
      std::uint8_t which = 0; // a BUFFER_QUERY, a SAMPLE_WINDOW or a SAMPLE_FILE
      std::int32_t channels = -1; // -1: not given
      float a = 0.f;
      float b = -1.f;
      std::uint32_t n = 0;
      std::uint32_t m = 0;

      // The reply: the buffer's shape once the message has run.
      std::uint32_t replyFrames = 0;
      std::uint32_t replyChannels = 0;
      float replyRate = 0.f;
      float replyMs = 0.f;

      char text[fileScheduler::PATH_CAPACITY] = {}; // a path, or a buffer name
    };

    /**
     *  @brief Read the ``~buffer`` message in @p length characters of
     *         @p text into @p out: ``read``, ``replace``, ``write``,
     *         ``writewave``, ``writeaiff``, ``writeflac``, ``clear``,
     *         ``fill``, ``apply``, ``normalize``, ``setsize``,
     *         ``sizeinsamps``, ``crop``, ``crop_samples``, ``duplicate``,
     *         ``sr`` and the queries ``getsamps``, ``getsize``, ``getsr``,
     *         ``getchans``.
     *
     *  Returns false for anything else or a malformed message (a path too
     *  long to carry included). ``readagain`` is the sender's to handle: it
     *  needs the last read the sender remembers. Any thread, no allocation:
     *  the words are read in place.
     */
    bool ParseBufferMessage(const char* text, std::size_t length, bufferMessage& out);

    /** @brief Whether the message opens a file (``read`` or ``write``), whose
     *         name the host's path resolver gets to place (issue #1255). */
    inline bool BufferMessageOpensFile(const bufferMessage& message) {
      return message.op == BUFFER_OP::READ || message.op == BUFFER_OP::WRITE;
    }

    /** @brief The access a host's path resolver is asked for. */
    inline PATH_ACCESS BufferMessageAccess(const bufferMessage& message) {
      return message.op == BUFFER_OP::WRITE ? PA_WRITE : PA_READ;
    }

    /**
     *  @brief Do what @p message says to @p buffer, then put its shape in the
     *         message's reply fields.
     *
     *  @p path is the file to open for a ``read`` or ``write``: the host
     *  resolver's answer, or null for the name the message carries. Returns
     *  false when the edit failed (a missing file, a crop outside the buffer,
     *  a ``duplicate`` of a buffer that does not exist). Background or
     *  control thread only: it allocates and touches the disk.
     */
    bool ApplyBufferMessage(DSP::sampleBuffer& buffer, bufferMessage& message, const char* path);

  } // namespace PATCHER
} // namespace YSE

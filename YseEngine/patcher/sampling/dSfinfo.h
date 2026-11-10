#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sfinfo``: reports what a sound file holds — channels, sample
     *         size, sample rate, length, sample type and name (issue #1157).
     *
     *  Ported from Max's ``sfinfo~``. A control object that reads a file's
     *  header, never its samples, and has no ``YSE::DSP`` class behind it:
     *  there is no signal processing to put in one.
     *
     *  - Argument: ``file``, the sound file a ``bang`` reports on.
     *  - Inlet 0: ``open <file>`` names a file and reports on it; ``bang``
     *    reports on the last file named, by ``open`` or the argument.
     *  - Outlets, sent right to left: 5 the file's name (the last part of
     *    the name as given), 4 the sample type (``int8``, ``uint8``,
     *    ``int16``, ``int24``, ``int32``, ``float32``, ``float64``,
     *    ``mulaw``, ``alaw``, or ``other`` for an encoding without a fixed
     *    sample format, such as Ogg Vorbis), 3 the length in ms, 2 the
     *    sample rate in Hz, 1 the sample size in bits (0 for ``other``),
     *    0 the channel count. A file that cannot be opened sends nothing and
     *    counts in ``FailedTasks``.
     *
     *  ### The file is opened in the background
     *
     *  A message handler may run on the audio thread, and opening a file
     *  blocks. So ``open`` and ``bang`` copy the name into a fixed record and
     *  hand it to the patcher's ``fileScheduler`` with ``RequestPathTask``
     *  (wait-free, no allocation): the host's path resolver (issue #1255),
     *  when one is installed, places the name first, and the task reads the
     *  header with libsndfile on the background pool, through the host's
     *  virtual file system when that is active. The outlets fire at the top
     *  of a later block. The file is read again at every ``bang``, so a file
     *  that changed on disk is reported as it is now.
     *
     *  Limits that come from the scheduler, as for ``~buffer``: at most
     *  ``fileScheduler::CAPACITY`` requests in flight per patcher, results
     *  only while the patcher renders, nothing outside a patcher. A name of
     *  ``fileScheduler::PATH_CAPACITY`` characters or more is not taken.
     *
     *  Not ported: ``getnamed`` (it asks a named ``sfplay~``, which is not
     *  ported yet, issue #1155), ``open`` without a name and ``drag`` (Max
     *  opens a file dialog or takes a dropped file; the host draws and can
     *  send ``open <path>`` itself).
     */
    PATCHER_CLASS(dSfinfo, YSE::OBJ::D_SFINFO)
    _DO_MESSAGES
    _NO_CALCULATE
    _BANG_IN(Bang)

    _PARM_CLEAR
    _PARM_PARSE

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;

    /** @brief The file a bang reports on, empty for none. Tests. */
    const char* FileName() const {
      return name;
    }

    /** @brief Requests whose file could not be opened. Diagnostics and
     *         tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    // Hand a report request to the scheduler. Any thread, no allocation.
    void Request();

    // Copy `text` (of `length`) into `name`; false, and no change, when it
    // is empty or does not fit.
    bool SetName(const char* text, std::size_t length);

    // The filePathTaskFunction: background pool, never touches the object.
    struct sfinfoTask;
    static bool RunTask(char* args, std::size_t argBytes, const char* path);

    std::string fileArgument; // the argument

    // The file a bang reports on: the argument, or the last `open`. Written
    // by PARM_PARSE (before publication) and the message handlers only.
    char name[fileScheduler::PATH_CAPACITY] = {};

    // An outlet's text, built here so sending it allocates nothing: the
    // capacity is reserved at construction.
    std::string reply;

    std::atomic<std::uint64_t> failed{0};
  };

} // namespace PATCHER
} // namespace YSE

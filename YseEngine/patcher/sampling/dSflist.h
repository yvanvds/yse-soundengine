#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/fileStreamPlayer.hpp"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~sflist``: cues of sound files, kept under a name for
     *         ``~sfplay`` objects to play, holding a ``DSP::fileStreamCues``
     *         (issue #1155).
     *
     *  Ported from Max's ``sflist~``. Every ``~sfplay`` created with this
     *  name (or sent ``set <name>``) plays these cues, each through streams
     *  of its own, and opens them again when they change. A control object:
     *  no signal processing, and no outlets, as in Max.
     *
     *  - Arguments: ``name buffersize``. ``buffersize`` is read and has no
     *    effect: the engine's stream buffers are a fixed size.
     *  - Inlet 0: ``open <file>`` (the file a ``preload`` without one
     *    uses), ``fclose [file]`` (its cues removed; none: the open file),
     *    ``preload <cue> [file] [start ms] [end ms] [direction] [speed]``,
     *    ``preloadn`` (start and end from 0 to 1), ``clear [cues]`` and
     *    ``embed <0/1>``.
     *
     *  Each message is a ``fileScheduler`` task (files through the host's
     *  path resolver, issue #1255), since checking a file blocks; the task
     *  finds the table by name. Without a name there is nothing for an
     *  ``~sfplay`` to find, and the messages do nothing.
     *
     *  **Saving.** The arguments are saved; with ``embed 1`` the cues and
     *  the open file too, and loading defines them again in a table that
     *  has none yet.
     */
    PATCHER_CLASS(dSflist, YSE::OBJ::D_SFLIST)
    _DO_MESSAGES
    _NO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    /** @brief The table. Tests. */
    const std::shared_ptr<DSP::fileStreamCues>& Cues() const {
      return table;
    }

    /** @brief Requests refused, or whose file could not be opened.
     *         Diagnostics and tests. */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

  private:
    struct listTask;
    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);
    bool Submit(listTask& task, bool resolve);

    std::string tableName; // argument
    int bufferSize = 0; // argument, without effect
    // Made by PARM_PARSE, before any other thread sees the object.
    std::shared_ptr<DSP::fileStreamCues> table;
    aInt embed;
    std::atomic<std::uint64_t> failed{0};
  };

} // namespace PATCHER
} // namespace YSE

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"
#include "../io/fileScheduler.h"
#include "../../dsp/sampling/sampleBufferSet.hpp"
#include "bufferMessage.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~polybuffer``: a numbered collection of buffers, wrapping
     *         ``DSP::sampleBufferSet`` (issue #1139).
     *
     *  Ported from Max's ``polybuffer~``. The object called ``kit`` holds the
     *  buffers ``kit.1``, ``kit.2``, ... Each is an ordinary named buffer,
     *  so ``~play kit.2``, ``~groove kit.1`` or ``~waveform kit.3`` reach it
     *  as they reach a ``~buffer``. ``~polybuffer`` objects with the same
     *  name (in any patch, or the host's ``sampleBufferSet::acquire``) share
     *  one set.
     *
     *  - Arguments: ``name [embed] [quiet]``. Without a name the set gets
     *    one of its own, ``polybuffer-<n>`` (``getbufferlist`` tells it).
     *  - Messages: ``append <file>``, ``appendempty [ms] [channels]``,
     *    ``readfolder <folder>``, ``writetofolder <folder>``, ``clear``,
     *    ``send <index> <~buffer message>`` (index 0: every buffer) and the
     *    queries ``dump``, ``getbufferlist``, ``getcount``,
     *    ``getshortname``, ``getsize``.
     *  - Outlet 0: the answers. ``dump`` sends ``index name path ms
     *    channels rate`` per buffer (``none`` for a buffer read from no
     *    file); ``getbufferlist`` sends ``bufferlist name.1 name.2 ...``,
     *    then ``done``; ``getshortname`` sends ``name.N shortname`` per
     *    buffer (``none`` when it has no file), then ``done``; ``getcount``
     *    sends ``count N``; ``getsize`` sends ``size <bytes>``.
     *  - Outlet 1: a bang when a change has finished (an append, a folder,
     *    a clear, a send), and when the buffers an embedded patch restores
     *    through the host's path resolver have loaded.
     *
     *  ### Every message is background work
     *
     *  As with ``~buffer``, a message handler only fills a fixed record and
     *  hands it to the patcher's ``fileScheduler``; the background pool runs
     *  the tasks in the order they were sent, and the answers come back at
     *  the top of a later block. A task finds the set by its id, never
     *  through the object. The files ``append``, ``readfolder``,
     *  ``writetofolder`` and ``send``'s ``read`` / ``write`` name go to the
     *  host's path resolver first (issue #1255).
     *
     *  The record is the object's own, built at construction, so a message
     *  allocates nothing on any thread. Two messages arriving at once (from
     *  two threads) cannot share it: the second is dropped and counted
     *  (``Dropped()``). An answer is at most ``REPLY_CAPACITY`` bytes: the
     *  lines past it are left out and counted (``Truncated()``).
     *
     *  ### embed
     *
     *  With ``embed`` 1 the patch saves the buffers: the file each was read
     *  from, or the length and channels of an empty one, never the samples.
     *  Loading the patch adds them back in order, reading the files at once,
     *  or one after another in the background while the host resolves paths.
     *  Only the object that created the set restores it: a ``~polybuffer``
     *  joining a set that something else already holds adopts what it holds,
     *  and logs a warning unless ``quiet`` is 1.
     *
     *  Not ported: the dialogs (``append``, ``readfolder`` and
     *  ``writetofolder`` without a name), the information window (``open``,
     *  ``wclose``, double-click) and ``drag`` (dropping files on the box):
     *  the host owns windows and the mouse, and sends the messages instead.
     */
    PATCHER_CLASS(dPolybuffer, YSE::OBJ::D_POLYBUFFER)
    _DO_MESSAGES
    _NO_CALCULATE

    _PARM_CLEAR
    _PARM_PARSE

    /** @brief The longest answer one query sends, in bytes. */
    static constexpr std::size_t REPLY_CAPACITY = 16384;

    ~dPolybuffer() override;
    dPolybuffer(const dPolybuffer&) = delete;
    dPolybuffer& operator=(const dPolybuffer&) = delete;
    dPolybuffer(dPolybuffer&&) = delete;
    dPolybuffer& operator=(dPolybuffer&&) = delete;

    void SetParent(pObject* newParent) override;
    void DeliverFileResult(const fileResult& result, YSE::THREAD thread) override;
    bool DumpState(nlohmann::json::value_type& json) override;
    void RestoreState(const nlohmann::json::value_type& json) override;

    /** @brief The set this object names. Control thread, tests. */
    const std::shared_ptr<DSP::sampleBufferSet>& Set() const {
      return set;
    }

    /** @brief Tasks that ran and failed (a missing file, an empty folder, a
     *         send to a buffer the set does not have). */
    std::uint64_t FailedTasks() const {
      return failed.load(std::memory_order_relaxed);
    }

    /** @brief Messages dropped because another was being handled, or
     *         refused by a full scheduler. */
    std::uint64_t Dropped() const {
      return dropped.load(std::memory_order_relaxed);
    }

    /** @brief Answers that did not fit in ``REPLY_CAPACITY``. */
    std::uint64_t Truncated() const {
      return truncated.load(std::memory_order_relaxed);
    }

  private:
    struct polyTask;

    // Control thread: the set the name argument names.
    void Bind();

    // Hand `task` to the scheduler; `path` (may be null) goes to the host's
    // resolver first. Any thread, no allocation.
    bool Submit(polyTask& task, int tag, const char* path, PATH_ACCESS access);
    // Send the next embedded file to load through the resolver; false when
    // none is left.
    bool ContinueRestore();

    static bool RunTask(char* args, std::size_t argBytes);
    static bool RunPathTask(char* args, std::size_t argBytes, const char* path);

    std::string setName;
    int embed = 0;
    int quiet = 0;

    std::shared_ptr<DSP::sampleBufferSet> set;
    std::uint64_t setId = 0; // set on the control thread before publication
    bool createdSet = false;

    // The record the message handlers fill, under `busy`.
    std::unique_ptr<polyTask> request;
    std::atomic<bool> busy{false};

    // An embedded patch's files waiting for the resolver, loaded one at a
    // time: filled by RestoreState before the object is published, then read
    // by DeliverFileResult on the audio thread only.
    struct restoreEntry {
      std::size_t index;
      std::string path;
    };
    std::vector<restoreEntry> pending;
    std::size_t pendingNext = 0;
    std::unique_ptr<polyTask> restoreRequest;

    // An answer's line, sent from here so sending allocates nothing: the
    // capacity is reserved at construction.
    std::string line;

    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> truncated{0};
  };

} // namespace PATCHER
} // namespace YSE

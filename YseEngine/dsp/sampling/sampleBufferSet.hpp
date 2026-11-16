/*
  ==============================================================================

    sampleBufferSet.hpp
    A numbered collection of named sample buffers (issue #1139).

  ==============================================================================
*/

#ifndef YSE_DSP_SAMPLING_SAMPLEBUFFERSET_HPP
#define YSE_DSP_SAMPLING_SAMPLEBUFFERSET_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "sampleBuffer.hpp"

namespace YSE {
  namespace DSP {

    /**
     *  @brief A numbered collection of sample buffers that share a name:
     *         the store behind Max's ``polybuffer~``.
     *
     *  A set called ``kit`` holds the buffers ``kit.1``, ``kit.2``, ... in
     *  the order they were added. Each is an ordinary named
     *  ``sampleBuffer`` from ``sampleBufferRegistry``, so a ``~play kit.2``,
     *  a ``~groove kit.1`` or a host's ``sampleBufferRegistry::find("kit.3")``
     *  reaches it like any ``~buffer``. Load a sample kit with ``append``
     *  (one file at a time) or ``readFolder`` (every sound file in a folder,
     *  in the natural order of their names: ``kick2`` before ``kick10``).
     *
     *  **Getting one.** ``sampleBufferSet::acquire("kit")`` gives the set
     *  called ``kit``, creating it if nobody holds it yet: two patches (or
     *  a patch and the host) that name the same set share its buffers, as
     *  ``polybuffer~`` objects with the same name do. ``acquire("")`` makes
     *  a set with a name of its own, ``polybuffer-<id>``, so its buffers
     *  can still be reached by name. The set lives while something holds
     *  it; its buffers live while it, or anyone else, holds them.
     *
     *  **Indexes** count from 1, as the names do: ``buffer(1)`` is
     *  ``kit.1``.
     *
     *  **Buffers held elsewhere.** A name is free to be held before the set
     *  fills it: a ``~play kit.3`` made first holds an empty ``kit.3``, and
     *  the set's third buffer is that same buffer, so the player plays what
     *  the set loads. ``clear`` empties every buffer before letting go of
     *  it, so a player still naming ``kit.1`` falls silent rather than
     *  playing a buffer the set no longer lists.
     *
     *  **Threads.** Every member that changes the set loads or saves files,
     *  allocates and locks: control or background threads only, never the
     *  audio thread. Changes are serialised; the queries (``count``,
     *  ``buffer``, ``buffers``) take a short lock and do not wait for a load
     *  in progress, which shows up once it has finished. ``name``, ``id``,
     *  ``count`` and ``version`` are safe from any thread (``count`` and
     *  ``version`` are atomics). A reader on the audio thread reads the
     *  buffers themselves, through a ``sampleBufferReader``.
     *
     *  @code
     *    auto kit = YSE::DSP::sampleBufferSet::acquire("kit");
     *    kit->readFolder("samples/drums");        // kit.1, kit.2, ...
     *    kit->append("samples/crash.wav");          // the next one
     *    YSE::DSP::sampleBufferReader snare(kit->buffer(2));
     *  @endcode
     */
    class API sampleBufferSet {
    public:
      /**
       *  @brief The set called @p name, created empty when nobody holds it.
       *
       *  @param name     The set's name; its buffers are ``name.1``,
       *                  ``name.2``, ... An empty name makes a new set named
       *                  ``polybuffer-<id>``.
       *  @param created  When not null, set to whether this call created it.
       */
      static std::shared_ptr<sampleBufferSet> acquire(const std::string& name,
                                                      bool* created = nullptr);

      /** @brief The set called @p name, or null when nobody holds it. */
      static std::shared_ptr<sampleBufferSet> find(const std::string& name);

      /** @brief The set with this ``id``, or null when it no longer exists. */
      static std::shared_ptr<sampleBufferSet> findById(std::uint64_t id);

      /** @brief The name of the buffer at @p index (from 1) of the set
       *         called @p setName: ``setName.index``. */
      static std::string bufferName(const std::string& setName, std::size_t index);

      ~sampleBufferSet();
      sampleBufferSet(const sampleBufferSet&) = delete;
      sampleBufferSet& operator=(const sampleBufferSet&) = delete;
      sampleBufferSet(sampleBufferSet&&) = delete;
      sampleBufferSet& operator=(sampleBufferSet&&) = delete;

      /** @brief The set's name. Any thread. */
      const std::string& name() const {
        return setName;
      }

      /** @brief A number no other set has, for ``findById``. Any thread. */
      std::uint64_t id() const {
        return setId;
      }

      /** @brief How many buffers the set holds. Any thread. */
      std::size_t count() const;

      /** @brief Counts the changes to the set (a buffer added, the set
       *         cleared), not to the buffers' samples. Any thread. */
      std::uint64_t version() const;

      /** @brief The buffer at @p index, from 1; null outside 1 to
       *         ``count()``. */
      std::shared_ptr<sampleBuffer> buffer(std::size_t index) const;

      /** @brief Every buffer, in order: element 0 is ``buffer(1)``. */
      std::vector<std::shared_ptr<sampleBuffer>> buffers() const;

      /** @brief The name of the buffer at @p index, from 1 (whether or not
       *         the set holds that many). */
      std::string bufferName(std::size_t index) const;

      /**
       *  @brief The name of the file read into the buffer at @p index,
       *         without its folder and extension (``kick`` for
       *         ``drums/kick.wav``); empty when none was read or the set has
       *         no such buffer.
       */
      std::string shortName(std::size_t index) const;

      /**
       *  @brief The memory the buffers' samples take, in bytes: frames
       *         times channels times the size of a sample, summed.
       */
      std::size_t memoryBytes() const;

      // ─── changes ───────────────────────────────────────────────────────────

      /**
       *  @brief Read the sound file @p path into a new buffer at the end
       *         (Max's ``append``).
       *
       *  The buffer takes the file's channels, length and rate. Returns it,
       *  or null when the file cannot be read; the set is then unchanged.
       */
      std::shared_ptr<sampleBuffer> append(const char* path);

      /**
       *  @brief Add an empty buffer at the end (Max's ``appendempty``):
       *         @p ms long at the engine rate, @p channels channels (at
       *         least 1), all zero.
       *
       *  The length in frames is @p ms at ``SAMPLERATE`` as it is now; the
       *  buffer then follows the engine rate.
       */
      std::shared_ptr<sampleBuffer> appendEmpty(Flt ms, UInt channels = 1);

      /**
       *  @brief Read every sound file in the folder @p folder into new
       *         buffers at the end (Max's ``readfolder``).
       *
       *  Files are taken in the natural order of their names (digit runs
       *  compare as numbers, letters ignoring case), hidden files (a name
       *  starting with ``.``) and subfolders are skipped, and a file that
       *  is not a sound file adds nothing. Returns how many buffers were
       *  added; 0 when the folder cannot be read.
       */
      std::size_t readFolder(const char* folder);

      /**
       *  @brief Write every buffer to a sound file in the folder @p folder
       *         (Max's ``writetofolder``), creating the folder when it does
       *         not exist.
       *
       *  A buffer read from a file is written under that file's short name,
       *  any other under its buffer name (``kit.3``); a name used twice
       *  falls back to the buffer name. The extension follows @p format:
       *  ``.wav`` (``WAV`` or ``AUTO``), ``.aif`` or ``.flac``. An empty
       *  buffer is skipped. Returns how many files were written.
       */
      std::size_t writeToFolder(const char* folder, SAMPLE_FILE format = SAMPLE_FILE::WAV) const;

      /**
       *  @brief Empty every buffer (no frames, the engine rate, no file) and
       *         let go of them (Max's ``clear``). The next buffer added is
       *         ``name.1`` again.
       */
      void clear();

    private:
      sampleBufferSet(std::string name, std::uint64_t id);

      // The buffer the next append fills, emptied of its file details: the
      // named one at `index`.
      std::shared_ptr<sampleBuffer> slot(std::size_t index) const;
      void add(std::shared_ptr<sampleBuffer> buffer);

      const std::string setName;
      const std::uint64_t setId;

      // Serialises the changes, held across a file load. Taken before
      // `mutex`, never after it.
      mutable std::mutex editMutex;
      // Guards `entries`; held only to read or change the list.
      mutable std::mutex mutex;
      std::vector<std::shared_ptr<sampleBuffer>> entries;
      std::atomic<std::size_t> entryCount{0};
      std::atomic<std::uint64_t> changes{0};
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_SAMPLING_SAMPLEBUFFERSET_HPP

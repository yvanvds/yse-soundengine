/*
  ==============================================================================

    io.h
    Created: 23 Apr 2014 7:33:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include "headers/types.hpp"

namespace YSE {

  /**
   *  @brief Custom file-system callbacks for asset loading.
   *
   *  Game engines and packed-asset workflows often need libYSE to read sound
   *  files through their own VFS instead of the host operating system. The
   *  ``io`` singleton lets you install C-style callbacks for each step of the
   *  file lifecycle (open / read / seek / close / exists / length /
   *  getPosition). Call ``setActive(true)`` to switch the engine over.
   *
   *  Access through the free function ``IO()`` — do not instantiate.
   *
   *  @see YSE::IO
   *  @see YSE::BufferIO For an alternative that feeds sounds from in-memory buffers.
   */
  class API io {
  public:
    io();

    /** @brief Install the open callback.
     *
     *  The callback opens the named file, writes its size into ``filesize``,
     *  stores an implementation-defined handle in ``fileHandle``, and returns
     *  ``true`` on success.
     */
    io& open(bool (*funcPtr)(const char* filename, long long* filesize, void** fileHandle));

    /** @brief Install the close callback. */
    io& close(void (*funcPtr)(void* fileHandle));

    /** @brief Install the read callback.
     *
     *  Reads up to ``maxBytesToRead`` bytes into ``destBuffer``. Returns the
     *  number of bytes actually read, or 0 at end-of-file.
     */
    io& read(long long (*funcPtr)(void* destBuffer, long long maxBytesToRead, void* fileHandle));

    /** @brief Install the get-position callback. Returns the current read offset in bytes. */
    io& getPosition(long long (*funcPtr)(void* fileHandle));

    /** @brief Install the file-exists callback. */
    io& fileExists(bool (*funcPtr)(const char* filename));

    /** @brief Install the length callback. Returns the total size of the open file in bytes. */
    io& length(long long (*funcPtr)(void* fileHandle));

    /** @brief Install the seek callback.
     *
     *  ``whence`` matches the FILEPOINT enum (``FP_START``, ``FP_CURRENT``,
     *  ``FP_END``). Returns the new absolute position.
     */
    io& seek(long long (*funcPtr)(long long offset, int whence, void* fileHandle));

    /** @brief Enable or disable the custom IO layer.
     *
     *  When inactive, libYSE falls back to its default platform file I/O.
     */
    io& setActive(bool value);

    /** @brief Whether the custom IO callbacks are currently active. */
    Bool getActive();

  private:
    aBool active;
  };

  /** @brief Access the singleton custom-IO object. */
  API io& IO();
} // namespace YSE

#endif // IO_H_INCLUDED

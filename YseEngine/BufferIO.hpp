#ifndef BUFFERIO_H_INCLUDED
#define BUFFERIO_H_INCLUDED

#include "headers/defines.hpp"

namespace YSE {

  /**
   *  @brief Feed sound files into the engine from in-memory byte buffers.
   *
   *  ``BufferIO`` is the simpler alternative to the callback-based ``YSE::io``
   *  interface: register byte buffers under string IDs, then load sounds by
   *  passing those IDs as if they were file names. Typical use is bundling
   *  audio assets inside a game-engine resource pack or inside an Android
   *  APK where the regular file system is not accessible.
   *
   *  Construct one instance, call ``SetActive(true)``, add your buffers, then
   *  create sounds normally.
   *
   *  @see YSE::io For full callback-based VFS integration.
   */
  class API BufferIO {
  public:
    /**
     *  @brief Construct a BufferIO layer.
     *
     *  @param storeCopy When ``true``, ``AddBuffer`` copies the supplied bytes
     *                   so the caller can free its buffer immediately. When
     *                   ``false`` (default), the caller owns the memory and
     *                   must keep it alive for as long as the buffer is
     *                   registered.
     */
    BufferIO(bool storeCopy = false);

    /** @brief Enable or disable this BufferIO layer. */
    void SetActive(bool value);

    /** @brief Whether this BufferIO layer is currently active. */
    bool GetActive();

    /** @brief Whether a buffer is registered under the given ID. */
    bool BufferNameExists(const char* ID);

    /** @brief Whether the given byte buffer is currently registered. */
    bool BufferExists(char* buffer);

    /**
     *  @brief Register a byte buffer under an ID.
     *
     *  Sounds can then be created by passing ``ID`` where a file name would
     *  normally go. ``length`` is the size of the buffer in bytes.
     *
     *  @return ``true`` on success, ``false`` if the ID is already in use.
     */
    bool AddBuffer(const char* ID, char* buffer, int length);

    /** @brief Unregister a buffer by its ID. */
    bool RemoveBufferByName(const char* ID);

    /** @brief Unregister a buffer by its address. */
    bool RemoveBuffer(char* buffer);

    ~BufferIO();

  private:
    bool active;
    bool storeCopy;
  };

} // namespace YSE

#endif

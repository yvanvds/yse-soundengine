/*
  ==============================================================================

    threadName.h
    Created: 2026-11-10

    OS-visible names for the engine's threads (issue #1259), so a host that
    lists its process's threads (Linux /proc/<pid>/task/<tid>/comm, Windows
    GetThreadDescription, a debugger, a profiler) can tell the audio callback
    and the render workers from its own threads.

  ==============================================================================
*/

#ifndef THREAD_NAME_H_INCLUDED
#define THREAD_NAME_H_INCLUDED

#include <cstddef>

namespace YSE {
  namespace INTERNAL {

    // The longest name every platform keeps whole: Linux and Android allow 15
    // characters plus the terminator, and refuse a longer one outright. Longer
    // names are cut to this length rather than dropped.
    constexpr std::size_t MAX_THREAD_NAME = 15;

    // The names the engine gives its threads.
    constexpr const char* AUDIO_THREAD_NAME = "YSE audio";
    constexpr const char* MIDI_IN_THREAD_NAME = "YSE MIDI in";

    // Name the calling thread. Best-effort: a platform without a thread-name
    // API, or one that refuses the call, leaves the name as it was. Never
    // allocates on Linux, Android or macOS; on Windows the kernel copies the
    // description. Call once, at the start of a thread the engine owns.
    void setCurrentThreadName(const char* name) noexcept;

    // Name a thread the engine does not create — the audio backend's callback
    // thread, a MIDI driver's input thread — from inside the callback. The
    // first call on a thread names it; every later call on that thread is one
    // thread_local load and a branch, so it is safe at the top of the audio
    // callback (no allocation, no lock, no system call after the first). The
    // first name a thread is given wins, so a backend that delivers two kinds
    // of callback on one thread does not see its name flip.
    void nameForeignThreadOnce(const char* name) noexcept;

  } // namespace INTERNAL
} // namespace YSE

#endif // THREAD_NAME_H_INCLUDED

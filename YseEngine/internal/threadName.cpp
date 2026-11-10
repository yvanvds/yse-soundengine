/*
  ==============================================================================

    threadName.cpp
    Created: 2026-11-10

    OS-visible names for the engine's threads (issue #1259).

  ==============================================================================
*/

#include "threadName.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__) // __linux__ includes Android
#include <pthread.h>
#endif

namespace {

#if defined(_WIN32)
  // SetThreadDescription exists from Windows 10 1607. Resolved from kernel32
  // at load time rather than linked, so the library still loads on an older
  // system (where threads simply stay unnamed), and so the lookup — which
  // takes the loader lock — never runs on the audio callback.
  using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

  SetThreadDescriptionFn resolveSetThreadDescription() noexcept {
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel == nullptr) return nullptr;
    FARPROC proc = ::GetProcAddress(kernel, "SetThreadDescription");
    // Through a generic function pointer: a direct FARPROC cast trips
    // -Wcast-function-type on MinGW.
    return reinterpret_cast<SetThreadDescriptionFn>(reinterpret_cast<void (*)()>(proc));
  }

  const SetThreadDescriptionFn setThreadDescription = resolveSetThreadDescription();
#endif

  // Whether nameForeignThreadOnce() has named the calling thread yet.
  // Constant-initialised, so a plain TLS load with no guard variable.
  thread_local bool tForeignThreadNamed = false;

} // namespace

void YSE::INTERNAL::setCurrentThreadName(const char* name) noexcept {
  if (name == nullptr) return;
  // Cut to the common limit in a stack buffer: Linux refuses a longer name
  // (ERANGE) instead of truncating it.
  char shortName[MAX_THREAD_NAME + 1];
  std::size_t len = 0;
  while (len < MAX_THREAD_NAME && name[len] != '\0') {
    shortName[len] = name[len];
    ++len;
  }
  shortName[len] = '\0';

#if defined(_WIN32)
  if (setThreadDescription == nullptr) return;
  // The engine's names are ASCII, so widening is a per-character copy.
  wchar_t wide[MAX_THREAD_NAME + 1];
  for (std::size_t i = 0; i <= len; ++i)
    wide[i] = static_cast<wchar_t>(static_cast<unsigned char>(shortName[i]));
  (void)setThreadDescription(::GetCurrentThread(), wide);
#elif defined(__APPLE__)
  // macOS can only name the calling thread, hence the one-argument form.
  (void)::pthread_setname_np(shortName);
#elif defined(__linux__)
  (void)::pthread_setname_np(::pthread_self(), shortName);
#endif
}

void YSE::INTERNAL::nameForeignThreadOnce(const char* name) noexcept {
  if (tForeignThreadNamed) return;
  tForeignThreadNamed = true;
  setCurrentThreadName(name);
}

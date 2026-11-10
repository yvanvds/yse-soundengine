// See thread_names.hpp (issue #1259).

#include "thread_names.hpp"

#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__linux__) // includes Android
#include <filesystem>
#include <fstream>
#include <system_error>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

namespace {

#if defined(_WIN32)
  using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

  // Resolved like the engine resolves SetThreadDescription: absent before
  // Windows 10 1607.
  GetThreadDescriptionFn getThreadDescription() {
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel == nullptr) return nullptr;
    FARPROC proc = ::GetProcAddress(kernel, "GetThreadDescription");
    return reinterpret_cast<GetThreadDescriptionFn>(reinterpret_cast<void (*)()>(proc));
  }

  // The thread's description, narrowed (the engine's names are ASCII).
  std::string describe(HANDLE thread) {
    GetThreadDescriptionFn fn = getThreadDescription();
    if (fn == nullptr) return {};
    PWSTR wide = nullptr;
    std::string out;
    if (SUCCEEDED(fn(thread, &wide)) && wide != nullptr) {
      for (const wchar_t* p = wide; *p != L'\0'; ++p)
        out.push_back(static_cast<char>(*p));
    }
    if (wide != nullptr) ::LocalFree(wide);
    return out;
  }
#elif defined(__linux__)
  std::string readComm(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::string line;
    if (in) std::getline(in, line);
    return line;
  }
#endif

} // namespace

bool TestHelpers::threadNamesSupported() {
#if defined(_WIN32)
  return getThreadDescription() != nullptr;
#elif defined(__linux__) || defined(__APPLE__)
  return true;
#else
  return false;
#endif
}

std::string TestHelpers::currentThreadName() {
#if defined(_WIN32)
  return describe(::GetCurrentThread());
#elif defined(__linux__)
  const long tid = ::syscall(SYS_gettid);
  return readComm("/proc/self/task/" + std::to_string(tid) + "/comm");
#elif defined(__APPLE__)
  char name[64] = {};
  if (::pthread_getname_np(::pthread_self(), name, sizeof(name)) != 0) return {};
  return name;
#else
  return {};
#endif
}

std::vector<std::string> TestHelpers::processThreadNames(bool& supported) {
  std::vector<std::string> names;
#if defined(_WIN32)
  supported = getThreadDescription() != nullptr;
  if (!supported) return names;
  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    supported = false;
    return names;
  }
  const DWORD pid = ::GetCurrentProcessId();
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = ::Thread32First(snapshot, &entry); ok; ok = ::Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid) continue;
    HANDLE thread = ::OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (thread == nullptr) continue;
    names.push_back(describe(thread));
    ::CloseHandle(thread);
  }
  ::CloseHandle(snapshot);
#elif defined(__linux__)
  supported = true;
  std::error_code ec;
  for (const auto& task : std::filesystem::directory_iterator("/proc/self/task", ec))
    names.push_back(readComm(task.path() / "comm"));
#else
  supported = false;
#endif
  return names;
}

std::size_t TestHelpers::countThreadsNamed(const std::vector<std::string>& names,
                                           const std::string& name) {
  return static_cast<std::size_t>(std::count(names.begin(), names.end(), name));
}

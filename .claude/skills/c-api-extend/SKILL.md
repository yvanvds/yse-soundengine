---
name: c-api-extend
description: Use when extending the C API at YseEngine/c_api/ — wrapping a new engine class, method, enum, or callback — or when auditing the C API for drift from the engine's public surface. Applies the project's RT-safety, ownership, and ABI conventions established in PRs #63–#67. Triggers on phrases like "wrap X in the C API", "update the C API after I added Y to the engine", "audit C API drift", "expose <thing> to Dart", and similar. Do NOT trigger for engine-internal changes that don't need FFI exposure, or for modifications to existing C-API entry points that don't add surface.
---

# Extending the YSE C API for new engine surface

`YseEngine/c_api/` is the only entry point for FFI consumers (Dart, Python, …).
The C API has explicit rules — RT-safe callback bridges, opaque-handle
ownership, null-safe no-op semantics, `YSE_C_CALLBACK` ABI defence,
exception-to-`YseStatus` translation, enum drift guard — that the engine's
own public C++ API does not enforce. New wrapping must follow all of them; a
careless callback bridge can stall the audio thread.

This skill governs how to wrap new engine surface in the C API. Read it
fully before editing any file under `YseEngine/c_api/`.

## Issue tracking — GitHub Issues first

CLAUDE.md mandates an issue before code. For this skill:

- **File the issue first** with `gh issue create --title "c-api: ..."`. Use
  the `enhancement` or `task` template; tag the layer as `c-api`.
- **Branch from `dev`** as `<issue-number>-c-api-<short-slug>` and PR back
  to `dev`. Not `master` — see CLAUDE.md §1.
- **Read context** with `gh issue view <n>` before starting if the
  request mentions an existing issue number.

The `gh` CLI is authenticated in the project environment.

## When to invoke this skill

Yes:
- A new engine class / method / enum was added and needs C-ABI exposure.
- The user asks to audit the C API for drift from the engine.
- The user is starting a new C-API milestone (M3 buffer overloads,
  M5 patcher sources, etc. — see `yse_sound.h`'s top-of-file note).

No:
- Modifying behaviour of an *existing* C-API function without adding
  surface — that's a regular fix, use the normal workflow.
- Internal engine refactors that don't change the public C++ surface.
- Adding audio-thread-reachable callbacks (occlusion,
  `dspSourceObject` user callback, `customFileReader`) on a casual
  wrapping pass. Those need design work — see the "Callback bridge
  rules" block in [yse_c_internal.hpp](../../../YseEngine/c_api/yse_c_internal.hpp).

## The six conventions

Every new C-API entry point must respect all six.

### 1. Opaque handle shape and ownership

Each engine type that gets a C handle is declared with `typedef struct
YseFoo YseFoo;` and accessed only via `YseFoo*` pointers. The `.cpp` file
does `reinterpret_cast<YSE::foo*>(handle)`. **Never expose any C++ type,
class, namespace, reference, or template across the C ABI.**

Every typedef in its home header carries a one-line ownership comment:

```c
/* Owned — release with yse_foo_destroy. */
typedef struct YseFoo YseFoo;
```

```c
/* Borrowed singleton — owned by the engine, never destroy.
   Obtain via yse_foo_get(). */
typedef struct YseFoo YseFoo;
```

```c
/* Borrowed — owned by parent YsePatcher. Release with
   yse_patcher_delete_object(patcher, handle); never call a destroy on
   the handle directly. */
typedef struct YsePHandle YsePHandle;
```

Dual-shape types (e.g. `YseChannel` is owned via `yse_channel_create` but
borrowed via the pre-built accessors) get both lines.

Forward-declaration typedefs (in headers that don't own the type) point
the reader at the home header rather than duplicating the ownership note:

```c
/* Forward declarations — see yse_channel.h / yse_dsp.h for ownership. */
typedef struct YseChannel YseChannel;
typedef struct YseDspBuffer YseDspBuffer;
```

Canonical examples: [yse_patcher.h](../../../YseEngine/c_api/include/yse_c/yse_patcher.h)
(owned + borrowed pair), [yse_reverb.h](../../../YseEngine/c_api/include/yse_c/yse_reverb.h)
(dual-shape).

### 2. Header layout

Every `include/yse_c/yse_<module>.h`:

- Top-of-file block comment naming purpose, the mirrored engine source,
  scope notes, and the null-safe-no-op convention statement.
- `#ifndef` / `#define` / `#endif` include guards. No `#pragma once`.
- `#include "yse_common.h"` (and `yse_enums.h` if needed) — never an
  engine C++ header.
- `#ifdef __cplusplus / extern "C" { ... } / #endif` wraps the body.
- `YSE_C_API` on every exported function declaration.
- `YSE_C_CALLBACK` on every callback typedef.
- All booleans as `int` (0/1), never `bool`. All sizes as `size_t` or
  `unsigned int`. Strings as `const char*` (in) or `char* + size_t`
  (snprintf-style out).

Canonical example: [yse_sound.h](../../../YseEngine/c_api/include/yse_c/yse_sound.h).

### 3. Function shape

| Engine signature                                | C API shape                                                                       |
| ----------------------------------------------- | --------------------------------------------------------------------------------- |
| `void play()` — can't fail                      | `void yse_x_play(YseX* h)` — null-safe no-op                                      |
| `void setFoo(T v)`                              | `void yse_x_set_foo(YseX* h, T v)` — null-safe no-op                              |
| `T getFoo() const`                              | `T yse_x_get_foo(YseX* h)` — return `0` / `false` / `NULL` on null                |
| `bool create(...)` — can fail                   | `YseStatus yse_x_load_...(...)` — `set_last_error` on failure                     |
| `const char* getName() const`                   | `size_t yse_x_get_name(YseX* h, char* buf, size_t cap)` — snprintf style          |
| `std::string getBlob() const` (engine-internal) | Same as above; never return `const char*` to engine-owned storage that can free   |

The header-top convention paragraph captures the void-no-op rule once for
the whole header. Per-function comments are reserved for genuinely surprising
behaviour.

**Body pattern for fallible operations** (lift from [yse_sound.cpp](../../../YseEngine/c_api/yse_sound.cpp)):

```cpp
YSE_C_API YseStatus yse_x_load(YseX* h, const char* arg) {
  if (!h) return YSE_ERR_INVALID_HANDLE;
  if (!arg) return YSE_ERR_INVALID_ARGUMENT;
  try {
    if (!to_cpp(h)->load(arg)) {
      yse_c::set_last_error(std::string("x load failed for: ") + arg);
      return YSE_ERR_<specific>;
    }
    return YSE_OK;
  } catch (const std::exception& e) {
    yse_c::set_last_error(e.what());
    return YSE_ERR_EXCEPTION;
  } catch (...) {
    yse_c::set_last_error("yse_x_load: unknown C++ exception");
    return YSE_ERR_EXCEPTION;
  }
}
```

**Body pattern for void state-change**:

```cpp
YSE_C_API void yse_x_play(YseX* h) { if (h) to_cpp(h)->play(); }
```

**Body pattern for string-out**:

```cpp
YSE_C_API size_t yse_x_get_name(YseX* h, char* buf, size_t cap) {
  if (!h) { if (buf && cap > 0) buf[0] = '\0'; return 0; }
  return copy_string(to_cpp(h)->getName(), buf, cap);
}
```

Canonical example for the snprintf pattern: [yse_device.cpp](../../../YseEngine/c_api/yse_device.cpp).

### 4. Callback bridges — RT-safe shape

When the engine invokes a user-provided callback on a **non-host thread**
(audio callback, RtMidi input thread, file streaming worker, future
occlusion / `dspSourceObject` / `customFileReader` paths), the bridge:

1. Holds the callback + user_data pair as `std::atomic<>`. **Never
   `std::mutex` or `std::lock_guard`.**
2. Installs with `memory_order_release` stores (user_data first, then cb).
3. Dispatches with `memory_order_acquire` loads (cb first; return if null;
   then user_data).
4. **Does not `malloc` / `new` / construct `std::string` on dispatch when
   the bridge can fire from the audio callback.** For raw byte buffers
   needed by an async-Dart host, preallocate a per-handle pool keyed by
   the handle.
5. Uses `YSE_C_CALLBACK` on the typedef.
6. If the bridge transfers heap ownership to the host (Dart's
   `NativeCallable.listener` requires this), pair the callback with a
   `yse_<module>_free_message` function and document the contract beside
   the typedef.

Canonical examples:
- [yse_midi.cpp](../../../YseEngine/c_api/yse_midi.cpp) — `c_raw_bridge`,
  atomic-swap, malloc per call is acceptable because the RtMidi input
  thread is not the audio callback.
- [yse_log.cpp](../../../YseEngine/c_api/yse_log.cpp) — `CallbackBridge`,
  same atomic-swap shape, same Dart-ownership malloc.

The "Callback bridge rules" block at the head of
[yse_c_internal.hpp](../../../YseEngine/c_api/yse_c_internal.hpp) restates
these rules in detail. Read it before designing any new bridge.

### 5. Enum mirroring + drift guard

When the engine adds an enum value that should be visible from the C API:

1. Add a matching `Yse<Name>_<VALUE>` entry to
   [yse_enums.h](../../../YseEngine/c_api/include/yse_c/yse_enums.h).
   Mirror the integer value exactly. Use C-compatible `typedef enum {...}
   Yse<Name>;` syntax (not `enum class`).
2. Add a `YSE_ASSERT_ENUM(YSE_..., YSE::...);` line to
   [yse_enums_check.cpp](../../../YseEngine/c_api/yse_enums_check.cpp).
   The build fails loudly if the values drift.

If the engine adds a brand-new enum, add the full mirror block + a full
set of `YSE_ASSERT_ENUM` lines. The drift guard is the only safety net
until a generator replaces the hand-mirrored file.

### 6. Build + test integration

- **New `.cpp` file** → append to `YSE_C_API_SRCS` in
  [c_api/CMakeLists.txt](../../../YseEngine/c_api/CMakeLists.txt).
- **New public functions** → add at least one smoke test under
  `Tests/<module>/`. The doctest harness picks up `TEST_CASE` without
  further wiring; see existing tests for patterns. Tests that need a
  null device use [`TestHelpers::engineInit()`](../../../Tests/support/null_device.hpp).
- **Build with the project wrapper**, never raw cmake invocations:
  `python yse.py build && python yse.py test`. CTest must stay green
  across all 14 entries.

## Workflow

1. **Inventory the new engine surface.** Read the engine header (e.g.
   `YseEngine/sound/soundInterface.hpp`); list every public method,
   enum, and callback. Skip private helpers and the implementation
   pimpls.
2. **Map ownership.** For each new class, decide: *owned* (user
   create/destroy), *borrowed singleton*, or *borrowed by parent*.
3. **Plan the module file.** New subsystem → new `yse_<module>.h` +
   `yse_<module>.cpp` pair + CMakeLists entry. Extending an existing
   subsystem → add to the existing pair.
4. **Apply the six conventions** as you write. Lift bodies from the
   canonical examples — don't re-invent the shape.
5. **Update the enum mirror + drift guard** if any new enum value
   landed in the engine.
6. **Update [c_api/CMakeLists.txt](../../../YseEngine/c_api/CMakeLists.txt)**
   if new files were added.
7. **Add a test stub** under `Tests/<module>/`. At minimum: a
   `TEST_CASE` that exercises create/destroy and one method.
8. **Build + test:** `python yse.py build && python yse.py test`. Must
   be green.
9. **PR to `dev`** with a body that references the closing issue and
   lists every new public function in the summary.

## Hard refusals

Stop and surface a design note rather than emit code that:

- Contains `std::mutex` or `std::lock_guard` in any callback bridge body.
  (PR #63 had to remove these from `yse_log.cpp`; do not put them back.)
- Allocates memory (`malloc`, `new`, `std::string` construction) in a
  callback that fires on the audio thread. If the host genuinely needs
  byte ownership, design a preallocated pool.
- Exposes any C++ type, namespace, reference, or template in a public
  `yse_c/*.h` header.
- Catches `std::exception` and rethrows, swallows, or returns 0/NULL
  without first calling `yse_c::set_last_error`.
- Adds a void function that throws on null instead of being a null-safe
  no-op.
- Adds a callback typedef without `YSE_C_CALLBACK`.
- Adds an enum to `yse_enums.h` without a matching `YSE_ASSERT_ENUM` in
  `yse_enums_check.cpp`.

If the engine's design genuinely requires one of these (rare), file an
issue describing the conflict and wait for a decision before proceeding.

## Things this skill DOES NOT do

- Modify vendored dependencies under `dependencies/` or `build*/_deps/`.
- Add new public methods, classes, or enums to the engine. Engine changes
  go through the engine's own workflow first; this skill mirrors them.
- Refactor existing C-API surface unless the engine change requires it.
- Wrap callbacks that fire on the audio thread (occlusion,
  `dspSourceObject`, `customFileReader`) — those need additional design
  work (preallocated pools, stack-only return paths) per
  [yse_c_internal.hpp](../../../YseEngine/c_api/yse_c_internal.hpp)'s
  rules block. Surface a design proposal first.
- Modify CLAUDE.md or PROJECT_OVERVIEW.md unless the wrapping pass
  introduces a structural change worth documenting at the project root.

## Verification

Before reporting the wrapping task complete:

- `python yse.py build` succeeds with **no new warnings**.
- `python yse.py test` passes **14/14 CTest entries** (the project's
  full suite, not just the new test).
- `grep "std::mutex\|std::lock_guard" YseEngine/c_api/` returns
  **nothing** beyond pre-existing lines (there should be none after
  PR #63).
- Every new public function in `include/yse_c/*.h` is either covered by
  the header-top void-no-op convention or carries its own explanatory
  comment.
- Every new opaque typedef has the ownership one-liner.
- Every new callback typedef has `YSE_C_CALLBACK`.
- Every new enum value has a matching `YSE_ASSERT_ENUM` line.

If any of these fail, the wrapping is incomplete — keep iterating rather
than reporting done.

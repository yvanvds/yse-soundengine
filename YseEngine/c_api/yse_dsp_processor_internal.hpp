/*
  yse_dsp_processor_internal.hpp — the owning wrapper behind YseDspProcessor
  (issue #1173), shared by every yse_dsp_<category>.cpp that wraps the
  block-processing DSP classes of the MSP port.

  The engine classes (DSP::onePole, DSP::phasor, ...) are plain value types
  with no virtual members, so a handle cannot be deleted through a common base
  and the class of a handle cannot be asked at run time. Same problem, same
  answer as YseDspBuffer (#662): the vptr lives here, in a wrapper private to
  the C bridge, and never crosses the ABI. Two things ride on it:

  * one destroy for the whole family — yse_dsp_processor_destroy() deletes
    through processorHandle's virtual destructor, which runs the most-derived
    destructor and frees the size that was really allocated;
  * a class check — each wrapper type has its own tag, and get<H>() returns
    NULL for a handle of another class, so a binding that mixes handles up gets
    a no-op instead of undefined behaviour. The check is a pointer compare.

  Host data crosses as float arrays. Each wrapper owns `Inputs` scratch
  buffers, sized to STANDARD_BUFFERSIZE at construction, that stage the host's
  input arrays for the engine's buffer& calls. Processing walks the host array
  in blocks of at most STANDARD_BUFFERSIZE samples (the engine classes process
  at most that many per call) and only ever shrinks or regrows a scratch buffer
  within the capacity it was built with, which DSP::buffer guarantees never
  allocates (buffer::resize, #1035). So every process entry point is
  allocation-free, lock-free and never touches the last-error slot: it is safe
  to call from a host's own audio callback.

  Not every wrapped class processes samples. The filter-design toolkit (#1296:
  filterDesign, filterResponse, filterBands, zPlane) holds a design or an
  editor's state and hands out lists of coefficients, roots and curves. Its
  wrappers stage no input (handle<T, 0>) and its getters copy lists out
  snprintf-style; the same allocation-free rule holds for every call but the
  constructor.
*/

#pragma once

#include "yse_c/yse_dsp_processor.h"
#include "yse_c_internal.hpp"

#include "../dsp/buffer.hpp"
#include "../headers/constants.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace yse_c {
  namespace processor {

    // The polymorphic base every YseDspProcessor* points at.
    struct processorHandle {
      explicit processorHandle(const void* k) noexcept : kind(k) {}
      virtual ~processorHandle() = default;
      processorHandle(const processorHandle&) = delete;
      processorHandle& operator=(const processorHandle&) = delete;
      processorHandle(processorHandle&&) = delete;
      processorHandle& operator=(processorHandle&&) = delete;

      // Identifies the wrapper type (tag<H>()). Compared, never dereferenced.
      const void* const kind;
    };

    // One address per wrapper type: the class tag get<H>() compares.
    template <typename H> const void* tag() noexcept {
      static const char id = 0;
      return &id;
    }

    // No bridge-side state: the default for handle's State.
    struct noState {};

    // The wrapper for one engine class T with `Inputs` staged input arrays.
    // `State` is what the bridge keeps beside the object, for a class that
    // reports an event per engine block (an envelope's `finished()`) which a
    // host call spanning several blocks must report for the whole call
    // (yse_dsp_envelopes.cpp, #1298).
    template <typename T, unsigned Inputs, typename State = noState>
    struct handle final : processorHandle {
      using object_type = T;
      static constexpr unsigned inputs = Inputs;

      template <typename... Args>
      explicit handle(Args&&... args)
        : processorHandle(tag<handle>()), object(std::forward<Args>(args)...) {}

      T object;
      // Inputs == 0 is a class that holds design or editor state and stages no
      // samples (yse_dsp_filter_design.cpp, #1296): an empty array, so its
      // handle allocates no scratch.
      std::array<YSE::DSP::buffer, Inputs> scratch;
      State state{};
    };

    inline processorHandle* base(YseDspProcessor* p) noexcept {
      return reinterpret_cast<processorHandle*>(p);
    }

    // The wrapper behind `p` if it is an H, else NULL (also for a NULL p).
    template <typename H> H* get(YseDspProcessor* p) noexcept {
      processorHandle* b = base(p);
      if (b == nullptr || b->kind != tag<H>()) return nullptr;
      return static_cast<H*>(b);
    }

    // The engine object behind `p` if it is an H, else NULL.
    template <typename H> typename H::object_type* object(YseDspProcessor* p) noexcept {
      H* h = get<H>(p);
      return h != nullptr ? &h->object : nullptr;
    }

    // Apply `f` to the engine object if `p` is an H (the setter shape).
    template <typename H, typename F> void with(YseDspProcessor* p, F&& f) {
      if (auto* o = object<H>(p)) f(*o);
    }

    // `f(object)`, or `fallback` if `p` is not an H (the getter shape).
    template <typename H, typename R, typename F> R query(YseDspProcessor* p, R fallback, F&& f) {
      auto* o = object<H>(p);
      return o != nullptr ? static_cast<R>(f(*o)) : fallback;
    }

    // Copy a biQuad::coefficientSet (any type with a0 a1 a2 b1 b2) to
    // out[0..4] in that order. Returns 1, the C getters' success value.
    template <typename S> int copy_set(const S& c, float* out) noexcept {
      out[0] = c.a0;
      out[1] = c.a1;
      out[2] = c.a2;
      out[3] = c.b1;
      out[4] = c.b2;
      return 1;
    }

    // Allocate an H and return it as the opaque handle; NULL with
    // yse_last_error() set if construction threw. Constructors are control-
    // thread calls: they allocate.
    template <typename H, typename... Args>
    YseDspProcessor* make(const char* where, Args&&... args) noexcept {
      return guard<YseDspProcessor*>(where, nullptr, [&]() {
        // guard() catches bad_alloc and every other exception around this
        // lambda, which the check cannot see through.
        auto* h = new H(std::forward<Args>(args)...); // NOLINT(bugprone-unhandled-exception-at-new)
        return reinterpret_cast<YseDspProcessor*>(static_cast<processorHandle*>(h));
      });
    }

    // The block size the engine classes accept per call.
    constexpr unsigned BLOCK = YSE::STANDARD_BUFFERSIZE;

    // Copy `n` host samples into a scratch buffer, shrinking it to `n` (never
    // past the capacity it was built with, so this does not allocate).
    inline YSE::DSP::buffer& stage(YSE::DSP::buffer& scratch, const float* src,
                                   unsigned n) noexcept {
      scratch.resize(n);
      std::memcpy(scratch.getPtr(), src, sizeof(float) * n);
      return scratch;
    }

    // Copy up to `n` samples of an engine output buffer to the host; a NULL
    // destination is skipped. Samples the engine did not produce are written
    // as 0 so the host never reads stale memory.
    inline void emit(YSE::DSP::buffer& result, float* dst, unsigned n) noexcept {
      if (dst == nullptr) return;
      const unsigned have = std::min(n, result.getLength());
      std::memcpy(dst, result.getPtr(), sizeof(float) * have);
      if (have < n) std::memset(dst + have, 0, sizeof(float) * (n - have));
    }

    // Run `body(offset, count)` over [0, length) in engine-sized blocks.
    template <typename F> void blocks(unsigned length, F&& body) {
      for (unsigned offset = 0; offset < length; offset += BLOCK) {
        body(offset, std::min(BLOCK, length - offset));
      }
    }

    // Run `body(handle, offset, count)` over `length` samples in engine
    // blocks. Returns the samples processed: `length`, or 0 if `p` is not an
    // H. Shared by every category file that processes samples.
    template <typename H, typename F>
    unsigned int drive(YseDspProcessor* p, unsigned int length, F&& body) {
      H* h = get<H>(p);
      if (h == nullptr) return 0;
      blocks(length, [&](unsigned offset, unsigned count) { body(*h, offset, count); });
      return length;
    }

    // A generator block at constant settings: `run(h, count)` runs the engine
    // overload that takes a length.
    template <typename H, typename F>
    unsigned int generate(YseDspProcessor* p, float* out, unsigned int length, F&& run) {
      if (out == nullptr) return 0;
      return drive<H>(p, length,
                      [&](H& h, unsigned o, unsigned n) { emit(run(h, n), out + o, n); });
    }

    // A block that stages one host array: `run(h, staged)` runs the engine
    // overload that takes a buffer.
    template <typename H, typename F>
    unsigned int one_signal(YseDspProcessor* p, const float* a, float* out, unsigned int length,
                            F&& run) {
      if (a == nullptr || out == nullptr) return 0;
      return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
        emit(run(h, stage(h.scratch[0], a + o, n)), out + o, n);
      });
    }

    // The same with two staged host arrays.
    template <typename H, typename F>
    unsigned int two_signals(YseDspProcessor* p, const float* a, const float* b, float* out,
                             unsigned int length, F&& run) {
      if (a == nullptr || b == nullptr || out == nullptr) return 0;
      return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
        emit(run(h, stage(h.scratch[0], a + o, n), stage(h.scratch[1], b + o, n)), out + o, n);
      });
    }

    // The same with three staged host arrays.
    template <typename H, typename F>
    unsigned int three_signals(YseDspProcessor* p, const float* a, const float* b, const float* c,
                               float* out, unsigned int length, F&& run) {
      if (a == nullptr || b == nullptr || c == nullptr || out == nullptr) return 0;
      return drive<H>(p, length, [&](H& h, unsigned o, unsigned n) {
        emit(run(h, stage(h.scratch[0], a + o, n), stage(h.scratch[1], b + o, n),
                 stage(h.scratch[2], c + o, n)),
             out + o, n);
      });
    }

  } // namespace processor
} // namespace yse_c

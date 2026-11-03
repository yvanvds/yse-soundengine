#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include "../../dsp/sampling/sampleBuffer.hpp"

namespace YSE {
  namespace PATCHER {

    class pObject;

    /** @brief The ``fileResult::tag`` every ``bufferBinding`` task
     *         completion carries (issue #1256). */
    constexpr int BUFFER_BINDING_TAG = 1256;

    namespace detail {
      // The type-erased half of bufferBinding, in bufferBinding.cpp. A
      // binding is a `void*` here; `create` makes one bound to a buffer
      // (null unbinds) and `destroy` frees one. Both run off the audio
      // thread only.
      struct bindingOps {
        void* (*create)(std::shared_ptr<DSP::sampleBuffer> buffer);
        void (*destroy)(void* binding);
      };

      struct bindingMailbox;

      class bufferBindingCore {
      public:
        bufferBindingCore(bindingOps ops, void* first);
        ~bufferBindingCore();
        bufferBindingCore(const bufferBindingCore&) = delete;
        bufferBindingCore& operator=(const bufferBindingCore&) = delete;
        bufferBindingCore(bufferBindingCore&&) = delete;
        bufferBindingCore& operator=(bufferBindingCore&&) = delete;

        void* Current() const;
        bool Request(pObject& owner, const char* name, std::size_t length);
        bool RequestSet(pObject& owner, const std::string& message, bool& isSet);
        bool Take(pObject& owner);
        bool ReleasePending() const;

      private:
        std::shared_ptr<bindingMailbox> box; // made at construction, never replaced
      };
    } // namespace detail

    /**
     *  @brief A sampling object's buffer binding, rebound by name from any
     *         thread without allocating or locking there (issue #1256).
     *
     *  Binding a ``DSP::sampleBufferReader`` or ``DSP::sampleBufferWriter``
     *  looks the name up in the registry and attaches a hazard claim, which
     *  locks and allocates, while a ``set <name>`` message may arrive on the
     *  audio thread. This helper is the one way the sampling family does it,
     *  the mechanism ``~cycle`` uses for its table (#1192):
     *
     *  1. ``Request`` (or ``HandleSet``) packs the name into a fixed record
     *     and hands it to the patcher's ``fileScheduler`` as a ``TASK``. Any
     *     thread, wait-free, no allocation.
     *  2. On the background pool the task makes a new ``Binding``, binds it
     *     and leaves it in a one-slot mailbox. Of several requests before a
     *     take, the last wins.
     *  3. ``Take`` swaps it in with one atomic exchange, on the thread that
     *     uses ``Current()``. The binding it let go is freed by a further
     *     task, never on the audio thread.
     *
     *  The mailbox is shared by the object and its tasks and found by id in
     *  a process-wide table, so a task in flight survives the object's
     *  deletion (it frees what it made).
     *
     *  @tparam Binding default-constructible, with
     *          ``void bind(std::shared_ptr<DSP::sampleBuffer>)`` (null
     *          unbinds). ``DSP::sampleBufferReader`` and
     *          ``DSP::sampleBufferWriter`` qualify as they are; an object
     *          with more than one claim wraps them in a struct whose
     *          ``bind`` binds each (``~poke``'s two writers).
     *
     *  ### Wiring ``set`` into an object
     *
     *  @code
     *    // header:   bufferBinding<DSP::sampleBufferReader> binding;
     *    void dX::SetParent(pObject* p) { pObject::SetParent(p); EnableFileIO(); }
     *    PARM_PARSE() { binding.Bind(bufferName); }        // control thread
     *    MESSAGES()   { if (binding.HandleSet(*this, message)) return; ... }
     *    CALC() {
     *      binding.Take(*this);                            // top of the block
     *      DSP::sampleView view = binding.Current().pin();
     *      ...
     *    }
     *  @endcode
     *
     *  ``Take`` and ``Current`` belong to one thread at a time: ``CALC()``
     *  for a signal object, or whoever holds the object's ``exchange(true)``
     *  guard for one that reads in its message handlers (``~peek``). An
     *  object whose message path shares the binding with ``CALC()`` takes
     *  under that guard, and skips the take when the guard is busy (it
     *  comes again next block), as ``~poke`` does.
     *
     *  ### Limits, the scheduler's
     *
     *  A request is refused (``false``) when the name is
     *  ``fileScheduler::PATH_CAPACITY`` characters or longer, when the
     *  object is not in a patcher (``SetParent`` must call
     *  ``EnableFileIO``), or when the patcher has ``fileScheduler::CAPACITY``
     *  requests in flight (counted in ``FileIO()->Dropped()``). A scheduler
     *  slot is handed back by the patcher's next block, so with the audio
     *  stopped a few rebinds fill the table. The completions reach the
     *  owner's ``DeliverFileResult`` with ``tag == BUFFER_BINDING_TAG``; an
     *  object with tasks of its own ignores that tag.
     *
     *  **Saving.** A rebind is not saved: ``DumpJSON`` writes the name
     *  argument the object was created with. Document that on the object.
     */
    template <class Binding> class bufferBinding {
    public:
      /** @brief The tag a rebind's task completions carry. */
      static constexpr int TAG = BUFFER_BINDING_TAG;

      /** @brief An unbound binding, and its mailbox. Control thread
       *         (allocates and locks). */
      bufferBinding() : core(Ops(), new Binding()) {} // NOLINT(cppcoreguidelines-owning-memory)

      /** @brief Bind the current binding to buffer @p name now (empty
       *         unbinds). Control thread, while no other thread uses the
       *         object: ``PARM_PARSE``. */
      void Bind(const std::string& name) {
        Current().bind(name.empty() ? nullptr : DSP::sampleBufferRegistry::acquire(name));
      }

      /** @brief Ask for buffer @p name (@p length characters, empty
       *         unbinds) to be bound in the background. Any thread,
       *         wait-free, no allocation. False when refused. */
      bool Request(pObject& owner, const char* name, std::size_t length) {
        return core.Request(owner, name, length);
      }

      /** @brief ``set [name]``: when @p message's first word is ``set``,
       *         request its second word (none unbinds) and return true,
       *         whether or not the request was taken. False for any other
       *         message. Any thread, no allocation. */
      bool HandleSet(pObject& owner, const std::string& message) {
        bool isSet = false;
        core.RequestSet(owner, message, isSet);
        return isSet;
      }

      /** @brief Swap in a binding the background made, if one is waiting,
       *         and ask for the one let go to be freed. Wait-free, no
       *         allocation; the thread that uses ``Current()``. True when
       *         it swapped. */
      bool Take(pObject& owner) {
        return core.Take(owner);
      }

      /** @brief The binding in use. The thread that calls ``Take``; other
       *         threads only while none does (tests). */
      Binding& Current() {
        return *static_cast<Binding*>(core.Current());
      }
      const Binding& Current() const {
        return *static_cast<const Binding*>(core.Current());
      }

      /** @brief A binding let go by ``Take`` and not yet freed.
       *         Diagnostics and tests. */
      bool ReleasePending() const {
        return core.ReleasePending();
      }

    private:
      static void* Create(std::shared_ptr<DSP::sampleBuffer> buffer) {
        Binding* binding =
            new Binding(); // NOLINT(cppcoreguidelines-owning-memory): the mailbox owns it
        binding->bind(std::move(buffer));
        return binding;
      }
      static void Destroy(void* binding) {
        delete static_cast<Binding*>(binding); // NOLINT(cppcoreguidelines-owning-memory)
      }
      static detail::bindingOps Ops() {
        return detail::bindingOps{&Create, &Destroy};
      }

      detail::bufferBindingCore core;
    };

  } // namespace PATCHER
} // namespace YSE

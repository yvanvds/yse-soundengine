/*
  ==============================================================================

    signalBus.hpp
    Named signal buses: audio sent and received by name, without a cord
    (issue #1136).

  ==============================================================================
*/

#ifndef YSE_DSP_ROUTING_SIGNALBUS_HPP
#define YSE_DSP_ROUTING_SIGNALBUS_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../headers/constants.hpp"
#include "../buffer.hpp"

namespace YSE {
  namespace DSP {

    class signalBusSender;
    class signalBusReceiver;

    /**
     *  @brief A named signal bus: any number of senders, summed, heard by any
     *         number of receivers, with no cord between them.
     *
     *  Ported from Max's ``send~`` / ``receive~``, which ``~send`` and
     *  ``~receive`` wrap. A bus is found by name in one process-wide
     *  registry: ``acquire("drone")`` from C++, a patcher's ``~send`` and a
     *  ``~receive`` in another patcher all reach the same bus when they use
     *  the same name. A patcher scopes its names first, as ``.s`` and ``.r``
     *  do (``patcher.<patcher name>.<name>``), so the names a patch uses only
     *  meet the same names in patchers with the same patcher name.
     *
     *  Write to a bus through a ``signalBusSender`` and read it through a
     *  ``signalBusReceiver``. A receiver hears the sum of every sender bound
     *  to the bus.
     *
     *  **When a receiver hears a block.** Render threads may run senders and
     *  receivers in any order and at the same time, so a receiver reads
     *  each sender's *previous* block: the one written while ``block()``
     *  was one less than now. What it hears therefore does not depend on
     *  which thread got there first. A sender and a receiver that pass the
     *  same non-null *context* to ``write`` and ``read`` promise to run on one
     *  thread in a fixed order (a patcher passes itself). For them the
     *  receiver hears the sender's latest block instead: this block's when
     *  the sender has already run, the previous one when it runs later. That
     *  is how a patcher gives a receive after its sends the same block, and a
     *  receive inside a feedback loop one block of delay. A sender that
     *  wrote nothing in the last block is not heard.
     *
     *  **The block clock.** ``block()`` counts blocks. The engine calls
     *  ``advance()`` once before every block it renders. A program that
     *  renders without the engine calls it between blocks itself; without
     *  it every block is the same block, and only same-context pairs hear
     *  each other.
     *
     *  **Threads.** ``write`` and ``read`` allocate nothing, take no lock
     *  and never block: the audio thread uses them. ``bind`` (on both
     *  endpoints), ``acquire``, ``find`` and ``collect`` lock and allocate:
     *  control and background threads only, and one thread at a time per
     *  endpoint. ``bind`` may run while the audio thread is inside ``write``
     *  or ``read`` on the same endpoint. What it lets go (the old bus, the
     *  old sender storage, the old sender list) is kept until two more
     *  blocks have started, then freed by ``collect()``, which
     *  ``System().update()`` calls.
     */
    class API signalBus {
    public:
      /** @brief The bus called @p name, created when nobody holds it. */
      static std::shared_ptr<signalBus> acquire(const std::string& name);

      /** @brief The bus called @p name, or null when nobody holds it. */
      static std::shared_ptr<signalBus> find(const std::string& name);

      /** @brief How many named buses exist. */
      static std::size_t count();

      /** @brief Frees what ``bind`` let go, once two blocks have started
       *         since. ``System().update()`` calls it; ``bind`` does too. */
      static void collect();

      /** @brief How many items ``bind`` let go that are not freed yet.
       *         Diagnostics and tests. */
      static std::size_t retired();

      /** @brief Starts the next block. The audio thread, between blocks:
       *         the engine calls it before every block it renders. */
      static void advance();

      /** @brief The current block. Starts at 1. Any thread. */
      static std::uint64_t block();

      ~signalBus();
      signalBus(const signalBus&) = delete;
      signalBus& operator=(const signalBus&) = delete;
      signalBus(signalBus&&) = delete;
      signalBus& operator=(signalBus&&) = delete;

      /** @brief The bus's name. */
      const std::string& name() const {
        return busName;
      }

      /** @brief How many senders are bound to the bus. Takes the bus's
       *         lock. */
      UInt senders() const;

    private:
      // A sender's storage and the immutable list a receiver walks. Defined
      // in signalBus.cpp.
      struct slot;
      struct senderList;

      explicit signalBus(std::string name);
      friend class signalBusSender;
      friend class signalBusReceiver;

      // Control side: add or remove a sender's storage and publish the new
      // list. The list it replaces is retired, not freed.
      void attach(const std::shared_ptr<slot>& storage);
      void detach(const slot* storage);
      void publish(std::shared_ptr<const senderList> next);

      std::string busName;
      mutable std::mutex mutex; // guards `current`
      std::shared_ptr<const senderList> current; // the list `list` points at
      std::atomic<const senderList*> list{nullptr}; // what read() walks
    };

    /**
     *  @brief The sending end of a ``signalBus``.
     *
     *  ``write`` stores one block; every receiver of the bus hears it (see
     *  ``signalBus`` for which block it hears when). A sender that stops
     *  writing stops being heard after one block. Not copyable: each sender
     *  owns its storage on the bus.
     */
    class API signalBusSender {
    public:
      signalBusSender() = default;
      /** @brief Leaves the bus. Control thread. */
      ~signalBusSender();
      signalBusSender(const signalBusSender&) = delete;
      signalBusSender& operator=(const signalBusSender&) = delete;
      signalBusSender(signalBusSender&&) = delete;
      signalBusSender& operator=(signalBusSender&&) = delete;

      /** @brief Sends to @p bus from the next ``write`` on; null sends
       *         nowhere. Control thread; may overlap a ``write``. Binding
       *         again to the same bus changes nothing. */
      void bind(std::shared_ptr<signalBus> bus);

      /** @brief The bus bound now, or null. Control thread. */
      std::shared_ptr<signalBus> bus() const {
        return owner;
      }

      /** @brief Sends @p in as this block, at most ``STANDARD_BUFFERSIZE``
       *         samples. @p context: see ``signalBus``. Audio thread; once
       *         per block. Does nothing while unbound. */
      void write(buffer& in, const void* context = nullptr);

      /** @brief The next ``write`` sends silence instead of its input, which
       *         empties a feedback loop through the bus. Any thread. */
      void clear() {
        clearing.store(true, std::memory_order_release);
      }

    private:
      std::shared_ptr<signalBus> owner; // control side
      std::shared_ptr<signalBus::slot> storage; // control side
      std::atomic<signalBus::slot*> live{nullptr}; // what write() uses
      std::atomic<bool> clearing{false};
    };

    /**
     *  @brief The receiving end of a ``signalBus``.
     *
     *  ``read`` returns the sum of every sender of the bus (see
     *  ``signalBus`` for which block of each it hears). The output buffer is
     *  ``STANDARD_BUFFERSIZE`` long and allocated in the constructor.
     */
    class API signalBusReceiver {
    public:
      signalBusReceiver();
      ~signalBusReceiver();
      signalBusReceiver(const signalBusReceiver&) = delete;
      signalBusReceiver& operator=(const signalBusReceiver&) = delete;
      signalBusReceiver(signalBusReceiver&&) = delete;
      signalBusReceiver& operator=(signalBusReceiver&&) = delete;

      /** @brief Receives from @p bus from the next ``read`` on; null receives
       *         silence. Control thread; may overlap a ``read``. */
      void bind(std::shared_ptr<signalBus> bus);

      /** @brief The bus bound now, or null. Control thread. */
      std::shared_ptr<signalBus> bus() const {
        return owner;
      }

      /** @brief One block: the sum of the bus's senders, silence when
       *         unbound or when nobody sent. @p context: see ``signalBus``.
       *         Audio thread. */
      buffer& read(const void* context = nullptr);

      /** @brief The last block ``read`` returned. */
      buffer& output() {
        return out;
      }

    private:
      std::shared_ptr<signalBus> owner; // control side
      std::atomic<signalBus*> live{nullptr}; // what read() uses
      buffer out;
    };

  } // namespace DSP
} // namespace YSE

#endif // YSE_DSP_ROUTING_SIGNALBUS_HPP

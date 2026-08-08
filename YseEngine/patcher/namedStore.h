#pragma once
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief The patcher's shared-name registry — "give me the store called X"
     *         (issue #684).
     *
     *  ### The thing four objects deferred
     *
     *  Max shares data by name: "all ``coll`` objects that share the same name
     *  share their contents", "if two or more ``table`` objects share the same
     *  names, they also share the same values". Four objects in this patcher hit
     *  that requirement and each deferred it in turn — ``.coll`` (#494),
     *  ``.bag`` (#495), ``.funbuff`` (#497) and ``.table`` (#498) — and #498
     *  wrote down why the obvious candidate does not work:
     *  ``INTERNAL::NamedBus`` is a publish/subscribe **value** bus. It hands a
     *  copy of a published value to every subscriber; it holds no storage, it
     *  owns no lifetime, and it cannot answer "give me the object called X".
     *  Sharing *contents* needs a registry that keys real storage by name, which
     *  is this file.
     *
     *  ### It is ``.value``'s registry, generalised
     *
     *  ``AcquireValueSlot`` (``gValue.cpp``, #486) already does exactly this for
     *  one cell: a ``std::unordered_map`` from address to
     *  ``std::weak_ptr<valueSlot>`` behind a mutex, taken only on the control
     *  thread. Nothing about it was specific to a cell except the type, so this
     *  is that code with the type lifted out — and every rule it established is
     *  kept:
     *
     *  - **Control thread only.** ``AcquireNamedStore`` takes a mutex and may
     *    allocate, so it is reachable only from ``SetParams`` / ``SetParent`` /
     *    ``patcherImplementation::SetName`` and never from a message handler.
     *    That is not a stylistic preference: a patcher message handler runs on
     *    whichever thread dispatched the message and in-patcher delivery
     *    dispatches on ``T_DSP``, so a name resolved on a message path would be
     *    a mutex and an allocation on the audio callback. It is also why Max's
     *    ``refer`` — re-point this object at another name, at run time, from a
     *    message — is not portable to any object built on this registry.
     *  - **The registry holds stores weakly.** A name lives exactly as long as
     *    some object addresses it. Strong ownership would make every name a
     *    patch has ever spelled immortal for the life of the process — an
     *    unbounded leak for a headless engine that opens and closes patchers —
     *    and would leave one test's contents visible to the next.
     *  - **One namespace per store type.** ``Store`` selects the map, so a
     *    ``.coll`` called ``notes`` and a ``.table`` called ``notes`` are
     *    different things, which is Max's arrangement too.
     *  - **The address is ``"<patcherName>.<name>"``** by convention of every
     *    caller — the ``INTERNAL::NamedBus`` address form, so a shared store, a
     *    ``.s`` and a ``.r`` in one patcher all speak about one word. Building
     *    that address is the caller's job; this file only maps strings to
     *    storage.
     *
     *  What ``Store`` itself must provide is only default-constructibility. The
     *  real-time discipline lives there rather than here: a store reached from a
     *  message path is expected to be a table allocated whole in its constructor
     *  plus ``.value``'s non-blocking ``busy`` guard, so that a registry lookup
     *  happens once on the control thread and every later access is a pointer
     *  hop and one ``exchange``. ``collStore`` in ``gColl.h`` is the worked
     *  example.
     *
     *  ``gValue`` keeps its own copy of this map rather than being ported onto
     *  this template: it works, it is on nobody's critical path, and rewriting a
     *  shipped object is not what #684 is. ``.table``, ``.bag`` and ``.funbuff``
     *  adopt this one when their deferrals are picked up.
     */
    namespace NAMED_STORE {

      // One map per Store type. A struct rather than two statics so the mutex
      // and the map it guards cannot be initialised in the wrong order.
      template <class Store> struct registry {
        std::mutex mutex;
        std::unordered_map<std::string, std::weak_ptr<Store>> stores;
      };

      // Function-local so the map is built on first use rather than during
      // static initialisation, and so one instance exists per Store across every
      // translation unit that names it.
      template <class Store> registry<Store>& Registry() {
        static registry<Store> instance;
        return instance;
      }

      // Expired entries are cheap to leave and cheap to sweep, so they are swept
      // when the map has grown past a size no realistic patch reaches by itself.
      // Without this a long live-coding session — every patcher a fresh
      // "patcher_<N>" prefix — would accumulate one dead key per name it ever
      // spelled. `.value`'s threshold, for `.value`'s reason.
      constexpr std::size_t kPruneThreshold = 64;

      template <class Store> void PruneExpired(registry<Store>& reg) {
        for (auto it = reg.stores.begin(); it != reg.stores.end();) {
          if (it->second.expired()) {
            it = reg.stores.erase(it);
          } else {
            ++it;
          }
        }
      }

    } // namespace NAMED_STORE

    /**
     *  @brief The ``Store`` addressed by @p address, created if no object holds
     *         it yet; @p created reports which of the two happened.
     *
     *  Control thread only — it takes a mutex and may allocate.
     *
     *  @p created is the hook for "whoever brings the store into existence gets
     *  to say what is in it": ``.value``'s ``initial`` argument is applied only
     *  by the object that created the cell, and ``.coll``'s saved contents are
     *  restored only by the object that created the store. An object joining an
     *  established name **adopts what is already there**, because the
     *  alternative is that adding a second object to a patch silently rewinds
     *  the data every other object is reading.
     */
    template <class Store>
    std::shared_ptr<Store> AcquireNamedStore(const std::string& address, bool& created) {
      created = false;
      NAMED_STORE::registry<Store>& reg = NAMED_STORE::Registry<Store>();
      const std::lock_guard<std::mutex> lock(reg.mutex);

      auto it = reg.stores.find(address);
      if (it != reg.stores.end()) {
        if (std::shared_ptr<Store> existing = it->second.lock()) return existing;
        // The name outlived the last object that held it; the key is stale.
        reg.stores.erase(it);
      }

      if (reg.stores.size() >= NAMED_STORE::kPruneThreshold) NAMED_STORE::PruneExpired(reg);

      auto store = std::make_shared<Store>();
      reg.stores[address] = store;
      created = true;
      return store;
    }

    /**
     *  @brief How many names of this store type are currently held, expired keys
     *         excluded. Diagnostics and tests; control thread only.
     */
    template <class Store> std::size_t NamedStoreCount() {
      NAMED_STORE::registry<Store>& reg = NAMED_STORE::Registry<Store>();
      const std::lock_guard<std::mutex> lock(reg.mutex);
      std::size_t live = 0;
      for (const auto& entry : reg.stores) {
        if (!entry.second.expired()) live++;
      }
      return live;
    }

  } // namespace PATCHER
} // namespace YSE

/*
  ==============================================================================

    signalBus.cpp
    Named signal buses (issue #1136).

  ==============================================================================
*/

#include "signalBus.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <utility>

namespace YSE {
  namespace DSP {

    // One sender's storage. Two blocks, by parity of the block they were
    // written in: a sender writes block N into data[N & 1], so a receiver on
    // another render thread reads block N - 1 from the other half while the
    // sender writes this one. `stamp` says which block a half holds; the
    // release store of it publishes the samples and `length` before it.
    struct signalBus::slot {
      static constexpr std::uint64_t NEVER = std::numeric_limits<std::uint64_t>::max();

      buffer data[2]{buffer(STANDARD_BUFFERSIZE), buffer(STANDARD_BUFFERSIZE)};
      UInt length[2] = {0, 0};
      std::atomic<std::uint64_t> stamp[2] = {{NEVER}, {NEVER}};
      // The half written last, for a receiver of the same context.
      std::atomic<int> latest{0};
      // The context of the last write; null until the first.
      std::atomic<const void*> context{nullptr};
    };

    // The senders of a bus as one immutable list: a receiver walks it with
    // no lock while the control side publishes a new one. It holds its
    // slots, so a retired list keeps the storage it names alive.
    struct signalBus::senderList {
      std::vector<std::shared_ptr<slot>> slots;
    };

  } // namespace DSP
} // namespace YSE

using namespace YSE::DSP;

namespace {

  // Counts blocks; see signalBus::block(). Constant-initialised, so the audio
  // thread never meets a guard.
  //
  // The clock, the published pointers (a bus's list, an endpoint's `live`)
  // and their loads are seq_cst, which is what makes the two-block grace
  // hold. Retire() loads the clock after the store that unpublished an
  // item; an endpoint loads the clock before the pointer. If Retire() read
  // block K, its store comes before advance(K -> K + 1) in the single
  // order, so a reader whose clock load returned K + 1 or later loads the
  // new pointer: only readers of block K or earlier can hold the old one,
  // and they are out of their block once block K + 1 has started. On x86 the
  // seq_cst loads are plain loads.
  std::atomic<std::uint64_t> g_block{1};

  // ─── the registry ───────────────────────────────────────────────────────────
  //
  // The names, and what bind() let go. One lock; no bus lock is ever taken
  // while it is held (a bus's mutex may be held while retiring, so the order
  // is bus, then registry), and nothing is destroyed under it.
  struct registryState {
    std::mutex mutex;
    std::unordered_map<std::string, std::weak_ptr<signalBus>> names;
    struct retiredItem {
      std::shared_ptr<void> item;
      std::uint64_t block;
    };
    std::vector<retiredItem> retired;
  };

  registryState& Registry() {
    // Never destroyed: an endpoint in a static object may retire into it
    // during static destruction.
    static registryState* const instance =
        new registryState(); // NOLINT(cppcoreguidelines-owning-memory): leaked on purpose
    return *instance;
  }

  // Keep `item` until two blocks have started. A reader that loaded a pointer
  // to it did so inside a block that had started by now, and is out of that
  // block once the next one starts; the second is margin, as the patcher's
  // reclaimer keeps.
  void Retire(std::shared_ptr<void> item) {
    if (item == nullptr) return;
    registryState& reg = Registry();
    const std::lock_guard<std::mutex> lock(reg.mutex);
    reg.retired.push_back({std::move(item), g_block.load(std::memory_order_seq_cst)});
  }

} // namespace

// ─── signalBus ──────────────────────────────────────────────────────────────

signalBus::signalBus(std::string name) : busName(std::move(name)) {}

signalBus::~signalBus() = default;

std::shared_ptr<signalBus> signalBus::acquire(const std::string& name) {
  registryState& reg = Registry();
  std::shared_ptr<signalBus> made;
  {
    const std::lock_guard<std::mutex> lock(reg.mutex);
    auto it = reg.names.find(name);
    if (it != reg.names.end()) {
      if (std::shared_ptr<signalBus> existing = it->second.lock()) return existing;
    }
    made.reset(new signalBus(name)); // NOLINT(cppcoreguidelines-owning-memory): private ctor
    reg.names[name] = made;
  }
  return made;
}

std::shared_ptr<signalBus> signalBus::find(const std::string& name) {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  auto it = reg.names.find(name);
  return it == reg.names.end() ? nullptr : it->second.lock();
}

std::size_t signalBus::count() {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  std::size_t live = 0;
  for (const auto& entry : reg.names) {
    if (!entry.second.expired()) live++;
  }
  return live;
}

void signalBus::collect() {
  std::vector<registryState::retiredItem> due;
  {
    registryState& reg = Registry();
    const std::lock_guard<std::mutex> lock(reg.mutex);
    const std::uint64_t now = g_block.load(std::memory_order_seq_cst);
    auto keep = std::stable_partition(
        reg.retired.begin(), reg.retired.end(),
        [now](const registryState::retiredItem& r) { return now < r.block + 2; });
    std::move(keep, reg.retired.end(), std::back_inserter(due));
    reg.retired.erase(keep, reg.retired.end());
    // Names whose bus is gone.
    for (auto it = reg.names.begin(); it != reg.names.end();) {
      if (it->second.expired()) {
        it = reg.names.erase(it);
      } else {
        ++it;
      }
    }
  }
  // Freed here, outside the lock: a bus's destructor may run, and nothing
  // may be destroyed while the registry is held.
  due.clear();
}

std::size_t signalBus::retired() {
  registryState& reg = Registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  return reg.retired.size();
}

void signalBus::advance() {
  g_block.fetch_add(1, std::memory_order_seq_cst);
}

std::uint64_t signalBus::block() {
  return g_block.load(std::memory_order_acquire);
}

UInt signalBus::senders() const {
  const std::lock_guard<std::mutex> lock(mutex);
  return current == nullptr ? 0 : static_cast<UInt>(current->slots.size());
}

void signalBus::attach(const std::shared_ptr<slot>& storage) {
  const std::lock_guard<std::mutex> lock(mutex);
  auto next = std::make_shared<senderList>();
  if (current != nullptr) next->slots = current->slots;
  next->slots.push_back(storage);
  publish(std::move(next));
}

void signalBus::detach(const slot* storage) {
  const std::lock_guard<std::mutex> lock(mutex);
  if (current == nullptr) return;
  auto next = std::make_shared<senderList>();
  next->slots.reserve(current->slots.size());
  for (const std::shared_ptr<slot>& s : current->slots) {
    if (s.get() != storage) next->slots.push_back(s);
  }
  publish(std::move(next));
}

void signalBus::publish(std::shared_ptr<const senderList> next) {
  // Caller holds `mutex`. A receiver may have loaded the old list and be
  // walking it, so it is retired rather than dropped.
  list.store(next.get(), std::memory_order_seq_cst);
  std::shared_ptr<const senderList> old = std::move(current);
  current = std::move(next);
  Retire(std::const_pointer_cast<senderList>(old));
}

// ─── signalBusSender ────────────────────────────────────────────────────────

signalBusSender::~signalBusSender() {
  bind(nullptr);
}

void signalBusSender::bind(std::shared_ptr<signalBus> bus) {
  if (bus == owner) return;
  // Fresh storage for the new bus: nothing of the old bus's last blocks is
  // heard on the new one.
  std::shared_ptr<signalBus::slot> fresh;
  if (bus != nullptr) {
    fresh = std::make_shared<signalBus::slot>();
    bus->attach(fresh);
  }
  live.store(fresh.get(), std::memory_order_seq_cst);
  if (owner != nullptr) owner->detach(storage.get());
  // A write that loaded the old storage may still be filling it.
  Retire(std::move(storage));
  storage = std::move(fresh);
  owner = std::move(bus);
  signalBus::collect();
}

void signalBusSender::write(buffer& in, const void* context) {
  // The clock first, then the pointer: see g_block.
  const std::uint64_t now = g_block.load(std::memory_order_seq_cst);
  signalBus::slot* s = live.load(std::memory_order_seq_cst);
  if (s == nullptr) return;
  const int half = static_cast<int>(now & 1U);
  buffer& to = s->data[half];
  const UInt n = std::min(in.getLength(), to.getLength());
  Flt* out = to.getPtr();
  if (clearing.load(std::memory_order_relaxed) &&
      clearing.exchange(false, std::memory_order_acq_rel)) {
    std::fill(out, out + n, 0.f);
  } else {
    std::memcpy(out, in.getPtr(), static_cast<std::size_t>(n) * sizeof(Flt));
  }
  s->length[half] = n;
  s->context.store(context, std::memory_order_relaxed);
  s->stamp[half].store(now, std::memory_order_release);
  s->latest.store(half, std::memory_order_release);
}

// ─── signalBusReceiver ──────────────────────────────────────────────────────

signalBusReceiver::signalBusReceiver() : out(STANDARD_BUFFERSIZE) {}

signalBusReceiver::~signalBusReceiver() {
  // Nothing reads through this receiver any more, but another receiver of
  // the same bus may: dropping the reference is enough.
  live.store(nullptr, std::memory_order_release);
}

void signalBusReceiver::bind(std::shared_ptr<signalBus> bus) {
  if (bus == owner) return;
  live.store(bus.get(), std::memory_order_seq_cst);
  // A read that loaded the old bus may still be walking its list.
  Retire(std::move(owner));
  owner = std::move(bus);
  signalBus::collect();
}

buffer& signalBusReceiver::read(const void* context) {
  out = 0.f;
  // The clock first, then the pointers: see g_block.
  const std::uint64_t now = g_block.load(std::memory_order_seq_cst);
  signalBus* bus = live.load(std::memory_order_seq_cst);
  if (bus == nullptr) return out;
  const signalBus::senderList* senders = bus->list.load(std::memory_order_seq_cst);
  if (senders == nullptr) return out;
  Flt* sum = out.getPtr();
  const UInt length = out.getLength();
  for (const std::shared_ptr<signalBus::slot>& held : senders->slots) {
    signalBus::slot* s = held.get();
    int half = 0;
    if (context != nullptr && s->context.load(std::memory_order_relaxed) == context) {
      // Same context: one thread, a fixed order. The latest block, if it is
      // this one or the last.
      half = s->latest.load(std::memory_order_acquire);
      const std::uint64_t stamp = s->stamp[half].load(std::memory_order_acquire);
      if (stamp == signalBus::slot::NEVER || stamp + 1 < now) continue;
    } else {
      // Anyone else: exactly the previous block, which its sender is not
      // writing now.
      half = static_cast<int>((now - 1) & 1U);
      if (s->stamp[half].load(std::memory_order_acquire) != now - 1) continue;
    }
    const Flt* from = s->data[half].getPtr();
    const UInt n = std::min(length, s->length[half]);
    for (UInt i = 0; i < n; ++i)
      sum[i] += from[i];
  }
  return out;
}

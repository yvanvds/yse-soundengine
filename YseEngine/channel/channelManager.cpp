/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CHANNEL::managerObject& YSE::CHANNEL::Manager() {
  static managerObject m;
  return m;
}

YSE::CHANNEL::managerObject::managerObject()
  : mgrSetup(this),
    mgrDelete(this),
    outputAngles(nullptr),
    outputIsLFE(nullptr),
    outputChannels(0) {}

YSE::CHANNEL::managerObject::~managerObject() noexcept {
  try {
    // wait for jobs to finish
    mgrSetup.join();
    mgrDelete.join();

    // drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and will be freed when that list is cleared.
    implementationObject* drained;
    while (toLoadInbox.try_pop(drained)) {
      (void)drained;
    }

    // remove all objects that are still in memory
    toLoad.clear();
    inUse.clear();
    returns.clear();
    {
      std::scoped_lock lk(returnGraphMutex);
      returnNodes.clear();
      returnEdges.clear();
      returnGenPosted.clear();
    }
    implementations.clear();
    delete[] outputAngles;
    delete[] outputIsLFE;
  } catch (...) {
    INTERNAL::LogImpl().emit(E_ERROR, "CHANNEL::Manager destructor swallowed exception");
  }
}

void YSE::CHANNEL::managerObject::update() {
  // master channel is not in inUse list
  DEVICE::Manager().getMaster().sync();

  ///////////////////////////////////////////
  // drain the main→audio inbox of newly-set-up impls
  ///////////////////////////////////////////
  {
    implementationObject* p;
    while (toLoadInbox.try_pop(p))
      toLoad.push_front(p);
  }

  ///////////////////////////////////////////
  // check if there are implementations that need setup
  ///////////////////////////////////////////
  if (!toLoad.empty() && !mgrSetup.isQueued()) {
    toLoad.remove_if(implementationObject::canBeRemovedFromLoading);
    INTERNAL::Global().addSlowJob(&mgrSetup);
  }

  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
  }
  runDelete = false;

  ///////////////////////////////////////////
  // check if loaded implementations are ready
  //
  // When readyCheck succeeds we move the impl into inUse AND erase it from
  // toLoad in the same step. Deferring the toLoad-erasure to the next tick's
  // remove_if creates a use-after-free window: within this same update tick
  // the impl can subsequently transition through OBJECT_RELEASE→OBJECT_DELETE
  // in the inUse iteration below, runDelete is set, deleteJob is enqueued,
  // the slow-pool frees the impl, and the next remove_if call dereferences
  // the freed pointer (ASan-confirmed).
  ///////////////////////////////////////////
  {
    for (auto c = toLoad.front(); c.valid();) {
      implementationObject* ptr = c.get();
      if (ptr->readyCheck()) {
        // Unlink from toLoad before linking into inUse: both share the impl's
        // single `_mgrNext` link (issue #194).
        c.erase();
        inUse.push_front(ptr);
        ptr->doThisWhenReady();
      } else {
        c.next();
      }
    }
  }

  ///////////////////////////////////////////
  // sync implementations
  ///////////////////////////////////////////
  {
    for (auto c = inUse.front(); c.valid();) {
      implementationObject* ptr = c.get();
      ptr->sync();
      if (ptr->getStatus() == OBJECT_RELEASE) {
        c.erase();
        // Audio-thread-side disconnect: reparent any children to this
        // channel's parent and remove this channel from parent->children
        // BEFORE marking OBJECT_DELETE. The slow-pool's deleteJob filters
        // on OBJECT_DELETE so by the time it can free this impl, the
        // audio-thread-iterated lists no longer reference it.
        if (ptr->parent != nullptr &&
            ptr->connectedToParent.load(
                std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with
                                              // release in implementationObject ctor handshake
          ptr->childrenToParent();
          ptr->parent->disconnect(ptr);
          ptr->connectedToParent.store(
              false, std::memory_order_release); // NOSONAR S8417: intentional release — publishes
                                                 // audio-thread disconnect before slow-pool delete
        }
        // Sever every send this impl participates in (both directions) and, if
        // it is a return, unlink it from the `returns` list — on the audio
        // thread, BEFORE OBJECT_DELETE makes it eligible for the slow-pool free.
        // Guarantees no live send slot dereferences the freed impl (issue #165 §9).
        ptr->detachSends();
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue; // c already refers to the successor after erase()
      }
      c.next();
    }
  }
}

YSE::CHANNEL::implementationObject*
YSE::CHANNEL::managerObject::addImplementation(YSE::channel* head) {
  std::scoped_lock lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::CHANNEL::managerObject::setup(implementationObject* impl) {
  impl->setStatus(OBJECT_CREATED);
  // Hand off to the audio thread via the lock-free inbox.
  toLoadInbox.push(impl);
}

Bool YSE::CHANNEL::managerObject::empty() {
  return implementations.empty();
}

void YSE::CHANNEL::managerObject::destroy() {
  // Runs from system::close() after both thread pools have been joined and the
  // audio device is closed, with Global().active already false, so nothing
  // else can touch these lists and the impl destructors take their inactive
  // path (no audio-thread parent disconnect). Mirrors
  // REVERB::Manager().destroy() (issue #132).

  // These are no-ops once the pools are down, but mirror the destructor's
  // contract that no setup/delete job is mid-flight before the lists are torn.
  mgrSetup.join();
  mgrDelete.join();

  // Drain the main->audio inbox; the pointers it holds reference impls owned
  // by `implementations` and would dangle once that list is cleared below.
  implementationObject* drained;
  while (toLoadInbox.try_pop(drained)) {
    (void)drained;
  }
  toLoad.clear();
  inUse.clear();
  returns.clear();
  {
    std::scoped_lock lk(returnGraphMutex);
    returnNodes.clear();
    returnEdges.clear();
    returnGenPosted.clear();
  }
  {
    std::scoped_lock lk(implementationsMutex);
    // Each impl destructor nulls its interface's pimpl, clearing the persistent
    // master/named channels so the next System::init() re-creates them cleanly.
    implementations.clear();
  }
  runDelete = false;
}

YSE::channel& YSE::CHANNEL::managerObject::master() {
  return _master;
}

YSE::channel& YSE::CHANNEL::managerObject::FX() {
  return _fx;
}

YSE::channel& YSE::CHANNEL::managerObject::music() {
  return _music;
}

YSE::channel& YSE::CHANNEL::managerObject::ambient() {
  return _ambient;
}

YSE::channel& YSE::CHANNEL::managerObject::voice() {
  return _voice;
}

YSE::channel& YSE::CHANNEL::managerObject::gui() {
  return _gui;
}

UInt YSE::CHANNEL::managerObject::getNumberOfOutputs() {
  return outputChannels;
}

Flt YSE::CHANNEL::managerObject::getOutputAngle(UInt nr) {
  if (nr >= outputChannels) {
    return 0.f;
  } else {
    return outputAngles[nr];
  }
}

Bool YSE::CHANNEL::managerObject::getOutputIsLFE(UInt nr) {
  if (nr >= outputChannels || outputIsLFE == nullptr) {
    return false;
  } else {
    return outputIsLFE[nr];
  }
}

void YSE::CHANNEL::managerObject::setMaster(CHANNEL::implementationObject* impl) {
  impl->objectStatus = OBJECT_CREATED;
  impl->setup();
  DEVICE::Manager().setMaster(impl);
}

void YSE::CHANNEL::managerObject::setChannelConf(CHANNEL_TYPE type, Int outputs) {
  outputChannels = outputs;
  channelType = type;
}

void YSE::CHANNEL::managerObject::changeChannelConf() {
  const UInt count = outputChannels.load();
  delete[] outputAngles;
  delete[] outputIsLFE;
  // Value-initialise (the trailing ()): std::atomic has no default member
  // initialiser, so `new aFlt[n]` alone would leave every angle indeterminate.
  // Presets below only touch the outputs their layout defines, so any output a
  // preset skips (e.g. an LFE, or a channel beyond the layout) must already
  // read as 0° / not-LFE rather than garbage (issue #203).
  outputAngles = new aFlt[count]();
  outputIsLFE = new aBool[count]();
  switch (channelType.load()) {
  case CT_AUTO:
    setAuto(outputChannels);
    break;
  case CT_MONO:
    setMono();
    break;
  case CT_STEREO:
    setStereo();
    break;
  case CT_QUAD:
    setQuad();
    break;
  case CT_51:
    set51();
    break;
  case CT_51SIDE:
    set51Side();
    break;
  case CT_61:
    set61();
    break;
  case CT_71:
    set71();
    break;
  case CT_CUSTOM:
    break; // we've set number of outputs. CT_CUSTOM expects the positions will be
           // set later
  }

  REVERB::Manager().setOutputChannels(outputChannels);

  for (auto i = inUse.begin(); i != inUse.end(); i++) {
    (*i)->setup();
  }
}

void YSE::CHANNEL::managerObject::setAuto(Int count) {
  // `count` is the device's physical channel count. Map it onto the standard
  // layout with that many channels. The .1 layouts (6 = 5.1, 7 = 6.1, 8 = 7.1)
  // carry an LFE at the platform-standard index 3. Counts without a defined
  // channel order here (3, 5, >8) fall back to stereo rather than guessing a
  // speaker order (issue #203).
  switch (count) {
  case 1:
    setMono();
    break;
  case 2:
    setStereo();
    break;
  case 4:
    setQuad();
    break;
  case 6:
    set51();
    break;
  case 7:
    set61();
    break;
  case 8:
    set71();
    break;
  default:
    setStereo();
    break;
  }
}

void YSE::CHANNEL::managerObject::setAngle(UInt idx, Flt degrees) {
  if (idx < outputChannels.load()) {
    outputAngles[idx] = Pi / 180.0f * degrees;
  }
}

void YSE::CHANNEL::managerObject::setLFE(UInt idx) {
  if (idx < outputChannels.load()) {
    outputIsLFE[idx] = true;
  }
}

void YSE::CHANNEL::managerObject::setMono() {
  setAngle(0, 0.0f);
}

void YSE::CHANNEL::managerObject::setStereo() {
  setAngle(0, -90.0f);
  setAngle(1, 90.0f);
}

void YSE::CHANNEL::managerObject::setQuad() {
  setAngle(0, -45.0f);
  setAngle(1, 45.0f);
  setAngle(2, -135.0f);
  setAngle(3, 135.0f);
}

// 5.1 — platform channel order: FL FR FC LFE BL BR.
void YSE::CHANNEL::managerObject::set51() {
  setAngle(0, -45.0f); // front left
  setAngle(1, 45.0f); // front right
  setAngle(2, 0.0f); // front center
  setLFE(3); // low-frequency effects
  setAngle(4, -135.0f); // back left
  setAngle(5, 135.0f); // back right
}

// 5.1 with side surrounds — FL FR FC LFE SL SR.
void YSE::CHANNEL::managerObject::set51Side() {
  setAngle(0, -45.0f); // front left
  setAngle(1, 45.0f); // front right
  setAngle(2, 0.0f); // front center
  setLFE(3); // low-frequency effects
  setAngle(4, -90.0f); // side left
  setAngle(5, 90.0f); // side right
}

// 6.1 — platform channel order: FL FR FC LFE SL SR BC.
void YSE::CHANNEL::managerObject::set61() {
  setAngle(0, -45.0f); // front left
  setAngle(1, 45.0f); // front right
  setAngle(2, 0.0f); // front center
  setLFE(3); // low-frequency effects
  setAngle(4, -90.0f); // side left
  setAngle(5, 90.0f); // side right
  setAngle(6, 180.0f); // back center
}

// 7.1 — platform channel order: FL FR FC LFE BL BR SL SR.
void YSE::CHANNEL::managerObject::set71() {
  setAngle(0, -45.0f); // front left
  setAngle(1, 45.0f); // front right
  setAngle(2, 0.0f); // front center
  setLFE(3); // low-frequency effects
  setAngle(4, -135.0f); // back left
  setAngle(5, 135.0f); // back right
  setAngle(6, -90.0f); // side left
  setAngle(7, 90.0f); // side right
}

/////////////////////////////////////////////////////
// Send / return buses (issue #165)
/////////////////////////////////////////////////////

// ─── Audio-thread render helpers ───

void YSE::CHANNEL::managerObject::zeroReturnBuffers() {
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    (*i)->clearBuffers();
  }
}

void YSE::CHANNEL::managerObject::processReturns(implementationObject* master) {
  if (returns.empty()) return;

  // Returns are few, so a per-block scan for the max generation is trivial and
  // avoids having to track a running maximum across teardown.
  Int maxGen = 0;
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    if ((*i)->generation > maxGen) maxGen = (*i)->generation;
  }

  // Process generation by generation, ascending. Within a generation returns are
  // mutually independent (their sends only target strictly-higher generations,
  // enforced at wiring time), so all can be dispatched to the fast pool at once
  // and then joined + finalized serially in list order (deterministic += order).
  for (Int g = 0; g <= maxGen; ++g) {
    for (auto i = returns.begin(); i != returns.end(); ++i) {
      if ((*i)->generation == g) INTERNAL::Global().addFastJob(*i);
    }
    for (auto i = returns.begin(); i != returns.end(); ++i) {
      if ((*i)->generation == g) {
        (*i)->join();
        (*i)->finalizeReturn(master);
      }
    }
  }
}

void YSE::CHANNEL::managerObject::linkReturn(implementationObject* r) {
  returns.push_front(r);
}

void YSE::CHANNEL::managerObject::unlinkReturn(implementationObject* r) {
  returns.remove(r);
}

// ─── Control-thread wiring graph ───

bool YSE::CHANNEL::managerObject::returnReachable(implementationObject* from,
                                                  implementationObject* to) {
  // Depth-first walk from `from` over return→return edges; true if `to` is
  // reachable. Bounded by the (small) number of returns. Control thread, under
  // returnGraphMutex.
  if (from == to) return true;
  std::vector<implementationObject*> stack;
  std::unordered_set<implementationObject*> seen;
  stack.push_back(from);
  seen.insert(from);
  while (!stack.empty()) {
    implementationObject* n = stack.back();
    stack.pop_back();
    auto it = returnEdges.find(n);
    if (it == returnEdges.end()) continue;
    for (auto& [next, cnt] : it->second) {
      if (cnt <= 0) continue;
      if (next == to) return true;
      if (seen.insert(next).second) stack.push_back(next);
    }
  }
  return false;
}

void YSE::CHANNEL::managerObject::recomputeAndPostGenerations() {
  // Longest-path layering over the return→return DAG (returns are few; a simple
  // relaxation to a fixpoint is ample). generation(n) = 0 for a return with no
  // incoming return→return edge, else 1 + max(generation(pred)).
  std::unordered_map<implementationObject*, Int> gen;
  gen.reserve(returnNodes.size());
  for (auto* n : returnNodes)
    gen[n] = 0;

  for (std::size_t pass = 0; pass < returnNodes.size(); ++pass) {
    bool changed = false;
    for (auto& [from, tos] : returnEdges) {
      if (returnNodes.find(from) == returnNodes.end()) continue;
      const Int gf = gen[from];
      for (auto& [to, cnt] : tos) {
        if (cnt <= 0 || returnNodes.find(to) == returnNodes.end()) continue;
        if (gen[to] < gf + 1) {
          gen[to] = gf + 1;
          changed = true;
        }
      }
    }
    if (!changed) break;
  }

  // Message only the returns whose generation actually changed — a single scalar
  // write applied in sync() on the audio thread.
  for (auto* n : returnNodes) {
    const Int g = gen[n];
    auto it = returnGenPosted.find(n);
    if (it == returnGenPosted.end() || it->second != g) {
      returnGenPosted[n] = g;
      messageObject m;
      m.ID = SET_GENERATION;
      m.uintValue = (UInt)g;
      n->sendMessage(m);
    }
  }
}

void YSE::CHANNEL::managerObject::registerReturnGraph(implementationObject* r) {
  std::scoped_lock lk(returnGraphMutex);
  returnNodes.insert(r);
  returnGenPosted[r] = 0; // matches the impl's default generation (0)
}

void YSE::CHANNEL::managerObject::unregisterReturnGraph(implementationObject* r) {
  std::scoped_lock lk(returnGraphMutex);
  returnNodes.erase(r);
  returnEdges.erase(r); // outgoing edges
  for (auto& [from, tos] : returnEdges) // incoming edges
    tos.erase(r);
  returnGenPosted.erase(r);
  recomputeAndPostGenerations();
}

bool YSE::CHANNEL::managerObject::tryAddReturnEdge(implementationObject* from,
                                                   implementationObject* to) {
  std::scoped_lock lk(returnGraphMutex);
  // Adding from→to closes a cycle iff `to` can already reach `from`.
  if (returnReachable(to, from)) return false;
  returnEdges[from][to]++;
  recomputeAndPostGenerations();
  return true;
}

void YSE::CHANNEL::managerObject::removeReturnEdge(implementationObject* from,
                                                   implementationObject* to) {
  std::scoped_lock lk(returnGraphMutex);
  auto it = returnEdges.find(from);
  if (it == returnEdges.end()) return;
  auto jt = it->second.find(to);
  if (jt == it->second.end()) return;
  if (jt->second > 0) --jt->second;
  if (jt->second <= 0) it->second.erase(jt);
  recomputeAndPostGenerations();
}

Int YSE::CHANNEL::managerObject::returnGenerationOf(implementationObject* r) {
  std::scoped_lock lk(returnGraphMutex);
  auto it = returnGenPosted.find(r);
  return it == returnGenPosted.end() ? -1 : it->second;
}

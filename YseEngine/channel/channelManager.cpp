/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#include <algorithm>
#include <iterator>
#include <memory>
#include "../internalHeaders.h"

YSE::CHANNEL::managerObject& YSE::CHANNEL::Manager() {
  static managerObject m;
  return m;
}

YSE::CHANNEL::managerObject::managerObject()
  : mgrSetup(this), mgrDelete(this), outputChannels(0), channelType(CT_AUTO) {}

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
    layout.store(nullptr);
    {
      std::scoped_lock lk(layoutMutex);
      layouts.clear();
    }
  } catch (...) {
    INTERNAL::EmitNoThrow(E_ERROR, "CHANNEL::Manager destructor swallowed exception");
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

  // Clear the delete request only once a job has actually been enqueued for it
  // (issue #990). isQueued() stays true while the previous delete job is still
  // *running*, and that job may already have walked past an impl the inUse pass
  // below marked OBJECT_DELETE on the last tick. Clearing the flag regardless
  // dropped that request, stranding the impl in `implementations` until some
  // other channel happened to be released — at the end of a churn, never. Keep
  // it for the next tick instead; the one spare delete pass this costs when the
  // job was merely queued is a no-op remove_if on the slow pool. Same fix as
  // SOUND::Manager's #817.
  if (runDelete && !mgrDelete.isQueued()) {
    INTERNAL::Global().addSlowJob(&mgrDelete);
    runDelete = false;
  }

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
        // Sever the insert plugin's back-reference here too, not in the
        // slow-pool destructor (issue #1179, the channel twin of #838). sync()
        // drops queued messages once the head is gone, so a detach sent just
        // before the channel was destroyed never applies; the destructor's clear
        // then raced the user destroying the plugin. The setStatus(OBJECT_DELETE)
        // store below publishes this to the slow pool.
        ptr->detachInsertDSP();
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue; // c already refers to the successor after erase()
      }
      c.next();
    }
  }

  ///////////////////////////////////////////
  // re-shape voice slices by measured cost (issue #861)
  //
  // Here, on a control tick between blocks, like every other membership
  // change: never inside a block, and never on a block without a tick, so a
  // render with no control work pending keeps its slice layout.
  ///////////////////////////////////////////
  if (getCostBalancing()) {
    const float target = sliceTargetCost();
    DEVICE::Manager().getMaster().rebalanceSlices(target);
    for (auto c = inUse.front(); c.valid(); c.next())
      c.get()->rebalanceSlices(target);
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
  // Audio-thread-only read set: `toLoad` and `inUse` are single-thread (audio)
  // by design, whereas `implementations` is mutated by the main thread
  // (addImplementation) and the slow-pool (deleteJob) under
  // implementationsMutex. Reading `implementations` here without that lock was
  // a latent #200-class race — no engine caller existed, but any future
  // audio-callback caller would have inherited it (issue #842). Mirrors
  // SOUND::managerObject::empty().
  return toLoad.empty() && inUse.empty();
}

std::size_t YSE::CHANNEL::managerObject::implementationCount() {
  std::scoped_lock lk(implementationsMutex);
  return static_cast<std::size_t>(std::distance(implementations.begin(), implementations.end()));
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
  // Every reader of the output layout is a pool job or the audio callback, and
  // both are down, so the layouts parked by setChannelConf() can go (issue
  // #1222). initShared() installs a fresh one on the next init.
  layout.store(nullptr);
  {
    std::scoped_lock lk(layoutMutex);
    layouts.clear();
  }
  // The render graph points into the impls just freed; forget it so the next
  // session rebuilds from its own master (issue #859).
  graphMaster = nullptr;
  INTERNAL::Global().renderer().markDirty();
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

// The layout is loaded once and the index is checked against its own capacity
// as well as the published count: setChannelConf() may publish a wider count
// between the two loads, and the layout loaded first can then be the narrower
// one it replaced (issue #1222).
Flt YSE::CHANNEL::managerObject::getOutputAngle(UInt nr) {
  const outputLayout* l = layout.load();
  if (l == nullptr || nr >= outputChannels || nr >= l->capacity) {
    return 0.f;
  }
  return l->angles[nr];
}

Bool YSE::CHANNEL::managerObject::getOutputIsLFE(UInt nr) {
  const outputLayout* l = layout.load();
  if (l == nullptr || nr >= outputChannels || nr >= l->capacity) {
    return false;
  }
  return l->isLFE[nr];
}

void YSE::CHANNEL::managerObject::setMaster(CHANNEL::implementationObject* impl) {
  impl->objectStatus = OBJECT_CREATED;
  impl->setup();
  DEVICE::Manager().setMaster(impl);
  INTERNAL::Global().renderer().markDirty(); // a new root for the render graph
}

void YSE::CHANNEL::managerObject::setChannelConf(CHANNEL_TYPE type, Int outputs) {
  // Control thread. Size the arrays for the new count *before* publishing it,
  // so no reader that sees the count can index past them (issue #1222). The
  // count used to be published here and the arrays reallocated only later, in
  // changeChannelConf() on the audio callback, so a slow-pool setup() reading
  // the new count indexed the old, narrower arrays. Growing here also keeps
  // the allocation off the audio thread; the layout replaced stays alive in
  // `layouts` for any reader that already loaded it.
  const UInt count = outputs > 0 ? static_cast<UInt>(outputs) : 0u;
  {
    std::scoped_lock lk(layoutMutex);
    const outputLayout* current = layout.load();
    if (current == nullptr || current->capacity < count) {
      layouts.push_back(std::make_unique<outputLayout>(count));
      layout.store(layouts.back().get());
    }
  }
  channelType = type;
  outputChannels = count;
}

Int YSE::CHANNEL::managerObject::outputsForLayout(CHANNEL_TYPE type, Int deviceOutputs) {
  // A device's output count is not always a set of speakers: ALSA's `default`
  // and `pipewire` PCMs on a PipeWire desktop advertise 128, a plugin ceiling.
  // Opening all of them rendered, metered and sent the mix into 126 outputs no
  // speaker uses (issue #1214).
  if (deviceOutputs <= 0) return 0;
  Int wanted = deviceOutputs;
  switch (type) {
  case CT_MONO:
    wanted = 1;
    break;
  case CT_STEREO:
    wanted = 2;
    break;
  case CT_QUAD:
    wanted = 4;
    break;
  case CT_51:
  case CT_51SIDE:
    wanted = 6;
    break;
  case CT_61:
    wanted = 7;
    break;
  case CT_71:
    wanted = 8;
    break;
  case CT_AUTO:
    // The counts setAuto() has a layout for keep every output; any other
    // count is placed as stereo, so only two outputs carry sound.
    switch (deviceOutputs) {
    case 1:
    case 2:
    case 4:
    case 6:
    case 7:
    case 8:
      break;
    default:
      wanted = 2;
      break;
    }
    break;
  case CT_CUSTOM:
  case CT_COUNT_:
    break;
  }
  return wanted < deviceOutputs ? wanted : deviceOutputs;
}

void YSE::CHANNEL::managerObject::changeChannelConf() {
  // Runs on the audio callback (deviceManager::doOnCallback()) and in
  // initShared(). Allocation-free: setChannelConf() already sized the layout.
  // The count is loaded before the layout, and setChannelConf() installs the
  // layout before the count, so a count read here always fits the layout read
  // after it; the min() only guards that invariant.
  const UInt published = outputChannels.load();
  outputLayout* l = layout.load();
  const UInt count = l == nullptr ? 0u : std::min(published, l->capacity);
  // Presets below only touch the outputs their layout defines, so any output a
  // preset skips (e.g. an LFE, or a channel beyond the layout) must read as
  // 0° / not-LFE rather than a value left by an earlier layout (issue #203).
  for (UInt i = 0; i < count; ++i) {
    l->angles[i] = 0.f;
    l->isLFE[i] = false;
  }
  if (l != nullptr) {
    applyPreset(*l, count);
  }

  REVERB::Manager().setOutputChannels(outputChannels);

  for (auto i = inUse.begin(); i != inUse.end(); i++) {
    (*i)->setup();
  }
}

void YSE::CHANNEL::managerObject::applyPreset(outputLayout& l, UInt count) {
  switch (channelType.load()) {
  case CT_AUTO:
    setAuto(l, count);
    break;
  case CT_MONO:
    setMono(l, count);
    break;
  case CT_STEREO:
    setStereo(l, count);
    break;
  case CT_QUAD:
    setQuad(l, count);
    break;
  case CT_51:
    set51(l, count);
    break;
  case CT_51SIDE:
    set51Side(l, count);
    break;
  case CT_61:
    set61(l, count);
    break;
  case CT_71:
    set71(l, count);
    break;
  case CT_CUSTOM:
    break; // we've set number of outputs. CT_CUSTOM expects the positions will be
           // set later
  case CT_COUNT_:
    break; // sentinel, not a layout
  }
}

void YSE::CHANNEL::managerObject::setAuto(outputLayout& l, UInt count) {
  // `count` is the device's physical channel count. Map it onto the standard
  // layout with that many channels. The .1 layouts (6 = 5.1, 7 = 6.1, 8 = 7.1)
  // carry an LFE at the platform-standard index 3. Counts without a defined
  // channel order here (3, 5, >8) fall back to stereo rather than guessing a
  // speaker order (issue #203).
  switch (count) {
  case 1:
    setMono(l, count);
    break;
  case 2:
    setStereo(l, count);
    break;
  case 4:
    setQuad(l, count);
    break;
  case 6:
    set51(l, count);
    break;
  case 7:
    set61(l, count);
    break;
  case 8:
    set71(l, count);
    break;
  default:
    setStereo(l, count);
    break;
  }
}

void YSE::CHANNEL::managerObject::setAngle(outputLayout& l, UInt count, UInt idx, Flt degrees) {
  if (idx < count) {
    l.angles[idx] = Pi / 180.0f * degrees;
  }
}

void YSE::CHANNEL::managerObject::setLFE(outputLayout& l, UInt count, UInt idx) {
  if (idx < count) {
    l.isLFE[idx] = true;
  }
}

void YSE::CHANNEL::managerObject::setMono(outputLayout& l, UInt count) {
  setAngle(l, count, 0, 0.0f);
}

void YSE::CHANNEL::managerObject::setStereo(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -90.0f);
  setAngle(l, count, 1, 90.0f);
}

void YSE::CHANNEL::managerObject::setQuad(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -45.0f);
  setAngle(l, count, 1, 45.0f);
  setAngle(l, count, 2, -135.0f);
  setAngle(l, count, 3, 135.0f);
}

// 5.1 — platform channel order: FL FR FC LFE BL BR.
void YSE::CHANNEL::managerObject::set51(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -45.0f); // front left
  setAngle(l, count, 1, 45.0f); // front right
  setAngle(l, count, 2, 0.0f); // front center
  setLFE(l, count, 3); // low-frequency effects
  setAngle(l, count, 4, -135.0f); // back left
  setAngle(l, count, 5, 135.0f); // back right
}

// 5.1 with side surrounds — FL FR FC LFE SL SR.
void YSE::CHANNEL::managerObject::set51Side(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -45.0f); // front left
  setAngle(l, count, 1, 45.0f); // front right
  setAngle(l, count, 2, 0.0f); // front center
  setLFE(l, count, 3); // low-frequency effects
  setAngle(l, count, 4, -90.0f); // side left
  setAngle(l, count, 5, 90.0f); // side right
}

// 6.1 — platform channel order: FL FR FC LFE SL SR BC.
void YSE::CHANNEL::managerObject::set61(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -45.0f); // front left
  setAngle(l, count, 1, 45.0f); // front right
  setAngle(l, count, 2, 0.0f); // front center
  setLFE(l, count, 3); // low-frequency effects
  setAngle(l, count, 4, -90.0f); // side left
  setAngle(l, count, 5, 90.0f); // side right
  setAngle(l, count, 6, 180.0f); // back center
}

// 7.1 — platform channel order: FL FR FC LFE BL BR SL SR.
void YSE::CHANNEL::managerObject::set71(outputLayout& l, UInt count) {
  setAngle(l, count, 0, -45.0f); // front left
  setAngle(l, count, 1, 45.0f); // front right
  setAngle(l, count, 2, 0.0f); // front center
  setLFE(l, count, 3); // low-frequency effects
  setAngle(l, count, 4, -135.0f); // back left
  setAngle(l, count, 5, 135.0f); // back right
  setAngle(l, count, 6, -90.0f); // side left
  setAngle(l, count, 7, 90.0f); // side right
}

/////////////////////////////////////////////////////
// Send / return buses (issue #165)
/////////////////////////////////////////////////////

void YSE::CHANNEL::managerObject::linkReturn(implementationObject* r) {
  returns.push_front(r);
  INTERNAL::Global().renderer().markDirty(); // the render graph lists returns
}

void YSE::CHANNEL::managerObject::unlinkReturn(implementationObject* r) {
  returns.remove(r);
  INTERNAL::Global().renderer().markDirty();
}

// ─── Render graph (issue #859) ───

void YSE::CHANNEL::managerObject::render(implementationObject& master) {
  INTERNAL::renderScheduler& scheduler = INTERNAL::Global().renderer();
  // D1: the channel and return lists are audio-thread-owned and every
  // structural change to them marks the graph dirty, so the graph is rebuilt
  // here, in place, between blocks — never while a worker is inside one.
  if (scheduler.isDirty() || graphMaster != &master) buildRenderGraph(master, scheduler);
  scheduler.run(master.mix);
}

void YSE::CHANNEL::managerObject::addChannelToGraph(implementationObject& ch,
                                                    INTERNAL::renderScheduler& scheduler) {
  // Pre-order: the channel's active voice slices (issue #860), then its
  // subtree. Consecutive slices land on consecutive workers' leaf lists. The
  // mix task waits for every slice and for every child's mix task.
  for (Int s = 0; s < ch.activeSlices; ++s) {
    scheduler.addLeaf(ch.slices[(std::size_t)s]);
  }
  Int deps = ch.activeSlices;
  for (auto i = ch.children.begin(); i != ch.children.end(); ++i) {
    addChannelToGraph(**i, scheduler);
    ++deps;
  }
  scheduler.setDependencies(ch.mix, deps);
}

void YSE::CHANNEL::managerObject::buildRenderGraph(implementationObject& master,
                                                   INTERNAL::renderScheduler& scheduler) {
  scheduler.beginBuild();
  addChannelToGraph(master, scheduler);

  Int masterChildren = 0;
  for (auto i = master.children.begin(); i != master.children.end(); ++i)
    ++masterChildren;

  // D4, coarse on purpose (returns are few): a return waits for every child of
  // the master — so, transitively, for every source channel, whose sends it
  // gathers — and for every lower-generation return, whose return->return
  // sends it gathers. The master waits for every return on top of its own leaf
  // and children.
  Int returnCount = 0;
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    implementationObject* r = *i;
    ++returnCount;
    Int deps = masterChildren;
    for (auto j = returns.begin(); j != returns.end(); ++j) {
      if ((*j)->generation < r->generation) ++deps;
    }
    // Nothing to wait for (a master without children, and generation 0): a
    // leaf, like any other dependency-free task.
    if (deps == 0)
      scheduler.addLeaf(r->mix);
    else
      scheduler.setDependencies(r->mix, deps);
  }
  // Every one of the master's own voice slices arrives too — not just one: a
  // master with a second slice (#860; by cost much sooner since #861) would
  // otherwise run its mix before that slice had finished writing `out`.
  scheduler.setDependencies(master.mix, master.activeSlices + masterChildren + returnCount);

  scheduler.endBuild();
  graphMaster = &master;
}

void YSE::CHANNEL::managerObject::arriveFromMasterChild(INTERNAL::renderScheduler& scheduler) {
  // Returns whose last dependency this was run inline here, one after the
  // other (D2: continuations are never queued).
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    scheduler.arrive((*i)->mix);
  }
}

void YSE::CHANNEL::managerObject::arriveFromReturn(implementationObject& r,
                                                   INTERNAL::renderScheduler& scheduler) {
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    if ((*i)->generation > r.generation) scheduler.arrive((*i)->mix);
  }
  if (graphMaster != nullptr) scheduler.arrive(graphMaster->mix);
}

void YSE::CHANNEL::managerObject::sumReturnsInto(implementationObject& master) {
  // List order, fixed between wiring changes: the fold is bit-identical
  // whichever threads rendered the returns.
  for (auto i = returns.begin(); i != returns.end(); ++i) {
    const implementationObject* r = *i;
    const std::size_t n = std::min(master.out.size(), r->out.size());
    for (std::size_t c = 0; c < n; ++c) {
      master.out[c] += r->out[c];
    }
  }
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

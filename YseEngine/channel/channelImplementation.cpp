/*
  ==============================================================================

  channelImplementation.cpp
  Created: 30 Jan 2014 4:21:26pm
  Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CHANNEL::implementationObject::implementationObject(channel* head)
  : head(head),
    // Initialise objectStatus in the ctor, not left to the first setStatus():
    // addImplementation() emplaces this impl into the mutex-guarded
    // `implementations` list BEFORE create() calls Manager().setup() (which sets
    // OBJECT_CREATED). In C++17 std::atomic's default ctor leaves the value
    // indeterminate, so during that window the slow-pool deleteJob —
    // `implementations.remove_if(canBeDeleted)`, canBeDeleted == (status ==
    // OBJECT_DELETE) — could read the garbage bytes as OBJECT_DELETE and free
    // the node the worker thread is still constructing (heap-layout-dependent
    // UAF / hang, issue #336). emplace_front runs under implementationsMutex and
    // the setup/delete jobs read under the same mutex, so this OBJECT_CONSTRUCTED
    // write is published before either job can observe the impl. SOUND and REVERB
    // already initialise objectStatus this way; CHANNEL was the outlier.
    objectStatus(OBJECT_CONSTRUCTED),
    newVolume(1.f),
    lastVolume(1.f),
    parent(nullptr),
    insert_dsp(nullptr),
    allowVirtual(true) {}

YSE::CHANNEL::implementationObject::~implementationObject() noexcept {
  try {
    // No render task of this channel can be running: the audio thread unlinks
    // it from the mix tree (and marks the render graph dirty) before
    // OBJECT_DELETE makes it eligible for this slow-pool free, and the graph is
    // rebuilt before the next block without dereferencing old tasks.

    // The primary disconnect path is on the audio thread, in
    // CHANNEL::Manager::update at the OBJECT_RELEASE→OBJECT_DELETE transition.
    // This guard ensures the slow-pool's destructor only touches
    // parent->children when no audio-thread disconnect has happened (i.e.
    // setup-failure path: channels that died before connect() ran).
    if (INTERNAL::Global().isActive()) {
      if (parent != nullptr &&
          connectedToParent.load(
              std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with
                                            // release in doThisWhenReady() /
                                            // Manager::update() for lock-free handshake
        parent->disconnect(this);
        childrenToParent();
      }
    }

    // Sever the insert plugin's back-reference before we die, so a plugin that
    // outlives this channel (e.g. a process-lifetime static destroyed at exit)
    // can't write through calledfrom into our freed insert_dsp field. Mirrors
    // the sound-path guard in ~SOUND::implementationObject (#298).
    if (insert_dsp != nullptr && insert_dsp->calledfrom != nullptr) {
      insert_dsp->calledfrom = nullptr;
    }

    if (head.load() != nullptr) {
      head.load()->pimpl = nullptr;
    }
  } catch (...) {
    INTERNAL::EmitNoThrow(E_ERROR, "CHANNEL::implementationObject destructor swallowed exception");
  }
}

Bool YSE::CHANNEL::implementationObject::connect(CHANNEL::implementationObject* ch) {
  // Any change to a `children` list changes the render graph (issue #859).
  INTERNAL::Global().renderer().markDirty();
  if (ch != this) {
    if (ch->parent != nullptr) ch->parent->disconnect(ch);
    ch->parent = this;
    children.push_front(ch);
    return true;
  } else
    ch->parent = nullptr;
  return false;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::CHANNEL::implementationObject* ch) {
  INTERNAL::Global().renderer().markDirty();
  children.remove(ch);
  return true;
}

Bool YSE::CHANNEL::implementationObject::connect(YSE::SOUND::implementationObject* s) {
  if (s->parent != nullptr && s->parent != this) {
    s->parent->disconnect(s);
  }
  s->parent = this;
  sounds.push_front(s);
  return true;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::SOUND::implementationObject* s) {
  sounds.remove(s);
  return true;
}

// ─── Render-graph task bodies (issue #859) ───
//
// A channel in the mix tree is two tasks: ownSoundsTask, a leaf that renders
// its own sounds, and mixTask, which runs once that leaf and every child
// channel's mixTask have arrived. A return is only a mixTask. The graph is
// built by CHANNEL::managerObject::buildRenderGraph(); these bodies must
// arrive() at every successor on every path, every block.

void YSE::CHANNEL::ownSoundsTask::execute(INTERNAL::renderScheduler& scheduler) {
  owner->renderOwnSounds();
  scheduler.arrive(owner->mix);
}

void YSE::CHANNEL::mixTask::execute(INTERNAL::renderScheduler& scheduler) {
  implementationObject& ch = *owner;
  if (ch.isReturn) {
    ch.renderReturn();
    CHANNEL::Manager().arriveFromReturn(ch, scheduler);
    return;
  }
  ch.renderMix();
  // The master is the root of the graph: nothing follows it.
  if (ch.parent == nullptr) return;
  // A child of the master finishing is what the returns wait for (coarse
  // dependencies, D4: once every child of the master is done, every source
  // channel is). Arrive there before the parent, which itself waits on them.
  if (ch.parent->parent == nullptr) CHANNEL::Manager().arriveFromMasterChild(scheduler);
  scheduler.arrive(ch.parent->mix);
}

void YSE::CHANNEL::implementationObject::renderOwnSounds() {
  // A channel with no sounds and no subchannels is skipped, as the old dsp()
  // skipped it: its buffer is not touched and its parent does not sum it. The
  // master is the exception — returns fold into it, so it must start from
  // silence even with nothing of its own to render.
  if (!hasWork()) {
    if (parent == nullptr) clearBuffers();
    return;
  }

  clearBuffers();
  for (auto i = sounds.begin(); i != sounds.end(); ++i) {
    if ((*i)->dsp()) {
      (*i)->toChannels();
    }
  }
}

void YSE::CHANNEL::implementationObject::sumChildren() {
  // Fixed list order, so the sum is bit-identical whichever threads rendered
  // the children (issue #857's golden test). A child without work left its
  // buffer untouched this block and is not summed.
  for (auto i = children.begin(); i != children.end(); ++i) {
    const implementationObject* child = *i;
    if (!child->hasWork()) continue;
    const std::size_t n = std::min(out.size(), child->out.size());
    for (std::size_t c = 0; c < n; ++c) {
      out[c] += child->out[c];
    }
  }
}

void YSE::CHANNEL::implementationObject::postMixProcess() {
  // Pre-fader insert chain over the whole bus — own sounds plus every
  // subchannel (D3 on issue #859; before it, a bus insert saw only the bus's
  // own sounds). The `out` vector is MULTICHANNELBUFFER-shaped, so it
  // type-matches dspObject::process (N-channel contract, #158).
  if (insert_dsp != nullptr) processInsertDSP();

  REVERB::Manager().process(this);

  // Publish pre-volume peak for VU consumers. The meter block is published
  // atomically and never resized in place (issue #839); the std::min guards
  // against a block whose count lags an `out` resize within the same
  // reconfiguration tick.
  if (meterBlock* m = meters.load(std::memory_order_acquire)) {
    const UInt n = (UInt)std::min(out.size(), (std::size_t)m->count);
    for (UInt i = 0; i < n; ++i) {
      m->pre[i].v.store(out[i].maxValue(), std::memory_order_release);
    }
  }
}

void YSE::CHANNEL::implementationObject::publishPostPeak() {
  // Post-volume peak for VU consumers — measured after adjustVolume() so the
  // reading reflects what listeners actually hear from this channel.
  if (meterBlock* m = meters.load(std::memory_order_acquire)) {
    const UInt n = (UInt)std::min(out.size(), (std::size_t)m->count);
    for (UInt i = 0; i < n; ++i) {
      m->post[i].v.store(out[i].maxValue(), std::memory_order_release);
    }
  }
}

void YSE::CHANNEL::implementationObject::renderMix() {
  // Mixer bus order (D3 on issue #859): fold children, then insert chain and
  // reverb, then sends and fader.
  if (parent == nullptr) {
    // The master: every child and every return (all finished — its mix task
    // depends on them), then its insert chain, reverb and fader. The master
    // has no send taps: a send from the master would feed a return the master
    // itself waits for. (Before the task graph they were accepted but dead —
    // tapped after the returns phase into buffers zeroed at the next block.)
    sumChildren();
    CHANNEL::Manager().sumReturnsInto(*this);
    postMixProcess();
    adjustVolume();
    publishPostPeak();
    return;
  }

  // A channel with no sounds or subchannels never filled `out` this block, so
  // its buffer is stale: it neither processes nor taps sends (and its parent
  // does not sum it). The fader still ramps, as it always has.
  const bool working = hasWork();
  if (working) {
    sumChildren();
    postMixProcess();
    // Pre-fader send taps read `out` before adjustVolume() scales it.
    if (!sends.empty()) runSendTaps(true);
  }

  adjustVolume();
  publishPostPeak();

  // Post-fader send taps read the faded `out` (the default aux-send behaviour).
  if (working && !sends.empty()) runSendTaps(false);
}

void YSE::CHANNEL::implementationObject::renderReturn() {
  // The return's input is the sum of this block's taps into it; its `out` is
  // rebuilt from silence every block (formerly zeroed by the master before the
  // source walk).
  clearBuffers();
  gatherSends();
  processReturnInsert();
  finalizeReturn();
}

void YSE::CHANNEL::implementationObject::removeInterface() {
  head.store(nullptr);
}

void YSE::CHANNEL::implementationObject::addDSP(DSP::dspObject* ptr) {
  // Detach any previously-attached plugin first and clear its back-reference
  // (calledfrom) — otherwise, when that old plugin is destructed later, its
  // destructor's `*calledfrom = nullptr` would write to this impl's insert_dsp
  // field, which may already have been freed. Same discipline as
  // SOUND::implementationObject::addDSP (the sound-path UAF, #298). Pointer
  // swap only: safe on the audio thread, no allocation or locking. A null
  // `ptr` simply detaches the chain.
  if (insert_dsp != nullptr) {
    insert_dsp->calledfrom = nullptr;
  }

  insert_dsp = ptr;
  if (insert_dsp != nullptr) {
    insert_dsp->calledfrom = &insert_dsp;
  }
}

void YSE::CHANNEL::implementationObject::processInsertDSP() {
  DSP::dspObject* ptr = insert_dsp;
  while (ptr != nullptr) {
    if (!ptr->bypass()) ptr->process(out);
    ptr = ptr->link();
  }
}

void YSE::CHANNEL::implementationObject::accumulateSend(sendSlot& s) {
  // src is this channel's `out` at the slot's tap point; dst is the slot's own
  // `tap` buffer, which only this channel's mix task writes and only the target
  // return's mix task reads — after this one, since the return depends on every
  // source (issue #859). Parallel sources therefore never share a buffer, and
  // the return sums the taps in its fixed registry order. The ramp is fused
  // into the multiply exactly like adjustVolume().
  implementationObject* tgt = s.target;
  // A send may be wired (ADD_SEND applied on the audio thread) before its target
  // return has finished setup() on the slow pool; until it reaches OBJECT_READY
  // it is not rendering, so there is nobody to gather the tap (issue #165). A
  // single atomic load, no lock or allocation.
  if (tgt->objectStatus.load(std::memory_order_acquire) != OBJECT_READY) return;
  // A return->return send is only ordered by generation: the target's task
  // depends on every lower-generation return. The wiring graph keeps targets
  // strictly higher, but the SET_GENERATION messages can trail an ADD_SEND by a
  // tick; skip the tap until they agree rather than write a slot the target
  // may be reading concurrently.
  if (isReturn && tgt->generation <= generation) return;
  const UInt n = (UInt)std::min(out.size(), s.tap.size());
  if (s.lastLevel == s.newLevel) {
    const Flt level = s.newLevel;
    if (level == 0.f) return; // soft-muted send: nothing to add
    for (UInt i = 0; i < n; ++i) {
      Flt* dst = s.tap[i].getPtr();
      const Flt* src = out[i].getPtr();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; ++j)
        dst[j] = src[j] * level;
    }
  } else {
    const Flt step = (s.newLevel - s.lastLevel) / STANDARD_BUFFERSIZE;
    for (UInt i = 0; i < n; ++i) {
      Flt* dst = s.tap[i].getPtr();
      const Flt* src = out[i].getPtr();
      Flt level = s.lastLevel;
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; ++j) {
        dst[j] = src[j] * level;
        level += step;
      }
    }
    s.lastLevel = s.newLevel;
  }
  s.tapped = true;
}

void YSE::CHANNEL::implementationObject::runSendTaps(bool preFaderPhase) {
  for (auto& s : sends) {
    if (s.target != nullptr && s.preFader == preFaderPhase) accumulateSend(s);
  }
}

void YSE::CHANNEL::implementationObject::gatherSends() {
  // Registry order is fixed between wiring changes (all applied on the audio
  // thread, between blocks), so the sum does not depend on which threads
  // rendered the sources.
  for (auto i = sendRegistry.begin(); i != sendRegistry.end(); ++i) {
    sendSlot* s = *i;
    if (!s->tapped) continue;
    const std::size_t n = std::min(out.size(), s->tap.size());
    for (std::size_t c = 0; c < n; ++c) {
      out[c] += s->tap[c];
    }
    s->tapped = false;
  }
}

void YSE::CHANNEL::implementationObject::processReturnInsert() {
  // `out` holds the gathered sends. Run the return's own DSP in place — its
  // insert chain is the effect (e.g. a plate reverb on a send return), plus any
  // attached global reverb. Single-writer over this return's own `out`.
  postMixProcess();
}

void YSE::CHANNEL::implementationObject::finalizeReturn() {
  // Pre-fader taps read the return's `out` before its own fader; post-fader
  // taps read it after (design §7). A return's send targets are always a
  // strictly higher generation, whose tasks depend on this one.
  if (!sends.empty()) runSendTaps(true);

  adjustVolume(); // the return's own fader
  publishPostPeak();

  if (!sends.empty()) runSendTaps(false);

  // The master's mix task folds the return in (sumReturnsInto). A return always
  // contributes — its insert chain may still be ringing out a reverb tail after
  // its senders fell silent — so there is no hasWork guard there.
}

void YSE::CHANNEL::implementationObject::detachSends() {
  // Audio-thread teardown at OBJECT_RELEASE, before the impl can be freed by the
  // slow pool (design §9). Two directions:
  //
  // 1. This channel's own outgoing sends: unlink each active slot from its
  //    target return's registry so the target never dangles.
  for (auto& s : sends) {
    if (s.target != nullptr) {
      s.target->sendRegistry.remove(&s);
      s.target = nullptr;
    }
    s.tapped = false;
  }

  // 2. If this is a return, sever every slot that still points at us (the
  //    many-to-one case) and unlink from the manager's returns list, so no live
  //    send slot keeps a pointer to us once we are freed.
  if (isReturn) {
    for (auto i = sendRegistry.begin(); i != sendRegistry.end();) {
      sendSlot* s = *i;
      ++i; // advance (reads s->regNext) BEFORE mutating the slot
      s->target = nullptr; // disable the sender's slot
      s->tapped = false;
    }
    sendRegistry.clear(); // detach every regNext link
    CHANNEL::Manager().unlinkReturn(this);
  }
}

void YSE::CHANNEL::implementationObject::setup() {
  if (objectStatus >= OBJECT_CREATED) {
    if (objectStatus == OBJECT_READY) return;

    const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
    out.resize(numOutputs);
    outConf.resize(numOutputs);
    publishMeterBlock(numOutputs);
    for (UInt i = 0; i < numOutputs; i++) {
      outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
      outConf[i].isLFE = CHANNEL::Manager().getOutputIsLFE(i);
    }
    computeEffectiveSpeakerWeights(outConf);

    // Size the send-slot vector exactly once, here on the slow pool, before the
    // channel ever reaches the render path (issue #165). Never resized
    // afterwards, so `&sends[i]` stays a stable back-reference node. Guarded so a
    // repeated setup() (e.g. readyCheck bouncing the status back to CREATED)
    // cannot reallocate it.
    if (!sendsSized) {
      sends.resize(sendSlotCount > 0 ? (std::size_t)sendSlotCount : 0);
      sendsSized = true;
    }
    sizeSendTaps(numOutputs);

    objectStatus = OBJECT_SETUP;
  }
}

void YSE::CHANNEL::implementationObject::resize(bool deep) {
  const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
  out.resize(numOutputs);
  outConf.resize(numOutputs);
  publishMeterBlock(numOutputs);
  for (UInt i = 0; i < numOutputs; i++) {
    outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
    outConf[i].isLFE = CHANNEL::Manager().getOutputIsLFE(i);
  }
  computeEffectiveSpeakerWeights(outConf);
  sizeSendTaps(numOutputs);
  if (deep) {
    for (auto i = children.begin(); i != children.end(); ++i) {
      (*i)->resize(true);
    }

    for (auto i = sounds.begin(); i != sounds.end(); ++i) {
      (*i)->resize();
    }
  }
}

void YSE::CHANNEL::implementationObject::sizeSendTaps(UInt numOutputs) {
  // Each slot's tap buffer holds one block per output (issue #859). Sized
  // wherever `out` is: on the slow pool before the channel is live, and on the
  // audio thread at a device reconfiguration, where `out` reallocates too.
  for (auto& s : sends) {
    s.tap.resize(numOutputs);
  }
}

void YSE::CHANNEL::implementationObject::computeEffectiveSpeakerWeights(
    std::vector<output>& outConf) {
  // The density-compensation term effective[i] = Σ_j overlap(angle_i, angle_j)
  // depends only on the speaker geometry, not on the source or the audio block.
  // Precompute it here (layout-change time) so toChannels() can read it instead
  // of paying the O(N^2) cos cost per source channel per block (issue #211).
  // LFE outputs are excluded from the azimuth pan, matching toChannels() which
  // skips them in both the i and j loops (issue #203); their effective is left
  // at the default and never read. The summation order over non-LFE speakers is
  // kept identical to the old inline loop so the result is bit-for-bit the same.
  for (UInt i = 0; i < outConf.size(); i++) {
    if (outConf[i].isLFE) continue;
    Flt sum = 0;
    for (UInt j = 0; j < outConf.size(); j++) {
      if (outConf[j].isLFE) continue;
      sum += SOUND::implementationObject::computeSpeakerOverlap(outConf[i].angle, outConf[j].angle);
    }
    outConf[i].effective = sum;
  }
}

Bool YSE::CHANNEL::implementationObject::readyCheck() {
  // this means we have don this check before and returned true back then.
  // the object is added to the list of inUse, but is probably not deleted just
  // yet. It will be deleted the next time the remove_if function runs (in objectManager)
  if (objectStatus == OBJECT_READY) {
    return false;
  }
  if (objectStatus == OBJECT_SETUP) {
    if (outConf.size() == CHANNEL::Manager().getNumberOfOutputs()) {
      objectStatus = OBJECT_READY;
      return true;
    }
  }
  objectStatus = OBJECT_CREATED;
  return false;
}

void YSE::CHANNEL::implementationObject::doThisWhenReady() {
  // A return bus is excluded from the source tree: it is NOT linked into any
  // parent's `children`, so it gets no own-sounds leaf and no parent. Instead it
  // joins the manager's audio-thread `returns` list, and the render graph makes
  // its mix task depend on the sources (issues #165, #859).
  // connectedToParent stays false, so the release/destructor parent-disconnect
  // path is skipped for returns (they tear down via detachSends()).
  if (isReturn) {
    CHANNEL::Manager().linkReturn(this);
    return;
  }

  parent->connect(this);
  // Audio thread now owns the link into parent->children. Mark it so the
  // release path can disconnect us before the slow-pool deleteJob frees us.
  connectedToParent.store(
      true, std::memory_order_release); // NOSONAR S8417: intentional release — publishes the
                                        // audio-thread connect to the dtor's acquire load
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::CHANNEL::implementationObject::getStatus() {
  return objectStatus.load();
}

void YSE::CHANNEL::implementationObject::setStatus(YSE::OBJECT_IMPLEMENTATION_STATE value) {
  objectStatus.store(value);
}

void YSE::CHANNEL::implementationObject::sync() {
  if (head.load() == nullptr) {
    objectStatus = OBJECT_RELEASE;
    return;
  }

  messageObject message;
  while (messages.try_pop(message)) {
    parseMessage(message);
  }
}

void YSE::CHANNEL::implementationObject::parseMessage(const messageObject& message) {
  switch (message.ID) {
  case ATTACH_REVERB:
    REVERB::Manager().attachToChannel(this);
    break;
  case ATTACH_DSP:
    addDSP((DSP::dspObject*)message.ptrValue);
    break;
  case MOVE: {
    channel* ptr = (channel*)message.ptrValue;
    if (ptr != nullptr) {
      ptr->pimpl->connect(this);
    }
    break;
  }
  case VIRTUAL:
    allowVirtual = message.boolValue;
    break;
  case VOLUME:
    newVolume = message.floatValue;
    break;
  case ADD_SEND: {
    // (Re)point a send slot at a return and link its back-reference. By the time
    // this arrives, setup() has already sized `sends` (a channel reaches sync()
    // only after readyCheck, which follows setup()), but bounds-check defensively.
    const Int slot = message.send.slot;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sendSlot& s = sends[slot];
      // Detach the slot's previous target from its registry before repointing.
      if (s.target != nullptr) s.target->sendRegistry.remove(&s);
      s.target = (implementationObject*)message.send.target;
      s.preFader = message.send.preFader;
      // Start the ramp from silence so a freshly-wired send fades in click-free;
      // the SEND_LEVEL message that follows sets the target level.
      s.lastLevel = 0.f;
      s.newLevel = 0.f;
      s.tapped = false;
      if (s.target != nullptr) s.target->sendRegistry.push_front(&s);
    }
    break;
  }
  case SEND_LEVEL: {
    const Int slot = message.sendLevel.slot;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sends[slot].newLevel = message.sendLevel.level;
    }
    break;
  }
  case REMOVE_SEND: {
    const Int slot = (Int)message.uintValue;
    if (slot >= 0 && (std::size_t)slot < sends.size()) {
      sendSlot& s = sends[slot];
      if (s.target != nullptr) {
        s.target->sendRegistry.remove(&s);
        s.target = nullptr;
      }
      s.lastLevel = 0.f;
      s.newLevel = 0.f;
      s.tapped = false;
    }
    break;
  }
  case SET_GENERATION:
    // A return's generation orders it in the render graph (issue #859).
    if (generation != (Int)message.uintValue) {
      generation = (Int)message.uintValue;
      INTERNAL::Global().renderer().markDirty();
    }
    break;
  }
}

void YSE::CHANNEL::implementationObject::childrenToParent() {
  // don't do this if there is no parent channel
  if (parent == nullptr) return;

  {
    auto i = children.begin();
    while (i != children.end()) {
      parent->connect(*i);
      i = children.begin();
    }
  }

  {
    auto i = sounds.begin();
    while (i != sounds.end()) {
      parent->connect(*i);
      i = sounds.begin();
    }
  }
}

void YSE::CHANNEL::implementationObject::clearBuffers() {
  for (UInt i = 0; i < out.size(); ++i) {
    out[i] = 0.0f;
  }
}

void YSE::CHANNEL::implementationObject::adjustVolume() {
  if (newVolume != lastVolume) {
    // new value, create a ramp
    Flt step = (newVolume - lastVolume) / STANDARD_BUFFERSIZE;

    for (UInt i = 0; i < out.size(); ++i) {
      Flt multiplier = lastVolume;
      Flt* ptr = out[i].getPtr();
      for (UInt j = 0; j < STANDARD_BUFFERSIZE; j++) {
        *ptr++ *= multiplier;
        multiplier += step;
      }
    }
    lastVolume = newVolume;
  } else {
    // same volume, just copy
    for (UInt i = 0; i < out.size(); ++i) {
      out[i] *= newVolume;
    }
  }
}

int YSE::CHANNEL::implementationObject::getNumOutputs() const {
  // Read the published meter block, not out.size(): `out` is resized in place
  // on the slow pool (setup) / audio thread (resize) and its metadata would
  // race a control-thread read exactly like the meters did (issue #839). The
  // block's count is set from the same numOutputs in the same call sites.
  const meterBlock* m = meters.load(std::memory_order_acquire);
  return m == nullptr ? 0 : static_cast<int>(m->count);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPre(int outputIdx) const {
  const meterBlock* m = meters.load(std::memory_order_acquire);
  if (m == nullptr || outputIdx < 0 || static_cast<UInt>(outputIdx) >= m->count) return 0.f;
  return m->pre[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPost(int outputIdx) const {
  const meterBlock* m = meters.load(std::memory_order_acquire);
  if (m == nullptr || outputIdx < 0 || static_cast<UInt>(outputIdx) >= m->count) return 0.f;
  return m->post[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPreCombined() const {
  const meterBlock* m = meters.load(std::memory_order_acquire);
  if (m == nullptr) return 0.f;
  float peak = 0.f;
  for (UInt i = 0; i < m->count; ++i) {
    const float v = m->pre[i].v.load(std::memory_order_acquire);
    if (v > peak) peak = v;
  }
  return peak;
}

float YSE::CHANNEL::implementationObject::getPeakLinearPostCombined() const {
  const meterBlock* m = meters.load(std::memory_order_acquire);
  if (m == nullptr) return 0.f;
  float peak = 0.f;
  for (UInt i = 0; i < m->count; ++i) {
    const float v = m->post[i].v.load(std::memory_order_acquire);
    if (v > peak) peak = v;
  }
  return peak;
}

void YSE::CHANNEL::implementationObject::publishMeterBlock(UInt numOutputs) {
  // Reuse the current block when the count is unchanged (e.g. a repeated
  // setup() pass): peaks keep their cells and no memory is retired.
  const meterBlock* current = meters.load(std::memory_order_relaxed);
  if (current != nullptr && current->count == numOutputs) return;
  // Never resize a published block: allocate a fresh one (zeroed cells) and
  // publish it with release so readers that acquire the pointer see fully
  // constructed storage. The old block stays alive in meterBlockOwner until
  // the impl is destroyed, so a concurrent reader can finish with it safely.
  meterBlockOwner.emplace_back(std::make_unique<meterBlock>(numOutputs));
  meters.store(meterBlockOwner.back().get(), std::memory_order_release);
}

/*
  ==============================================================================

    channelManager.cpp
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/


#include "../internalHeaders.h"

YSE::CHANNEL::managerObject & YSE::CHANNEL::Manager() {
  static managerObject m;
  return m;
}

YSE::CHANNEL::managerObject::managerObject() 
: mgrSetup( this), 
  mgrDelete(this), 
  outputAngles(nullptr),
  outputChannels(0) 
  {}

YSE::CHANNEL::managerObject::~managerObject() noexcept {
  try {
    // wait for jobs to finish
    mgrSetup.join();
    mgrDelete.join();

    // drain any pointers still queued by the main thread; they reference impls
    // owned by `implementations` and will be freed when that list is cleared.
    implementationObject * drained;
    while (toLoadInbox.try_pop(drained)) { (void)drained; }

    // remove all objects that are still in memory
    toLoad.clear();
    inUse.clear();
    implementations.clear();
    delete[] outputAngles;
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
    implementationObject * p;
    while (toLoadInbox.try_pop(p)) toLoad.emplace_front(p);
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
    auto previous = toLoad.before_begin();
    for (auto i = toLoad.begin(); i != toLoad.end(); ) {
      implementationObject * ptr = *i;
      if (ptr->readyCheck()) {
        inUse.emplace_front(ptr);
        ptr->doThisWhenReady();
        i = toLoad.erase_after(previous);
      } else {
        previous = i;
        ++i;
      }
    }
  }

  ///////////////////////////////////////////
  // sync implementations
  ///////////////////////////////////////////
  {
    auto previous = inUse.before_begin();
    for (auto i = inUse.begin(); i != inUse.end();) {
      (*i)->sync();
      if ((*i)->getStatus() == OBJECT_RELEASE) {
        implementationObject * ptr = (*i);
        i = inUse.erase_after(previous);
        // Audio-thread-side disconnect: reparent any children to this
        // channel's parent and remove this channel from parent->children
        // BEFORE marking OBJECT_DELETE. The slow-pool's deleteJob filters
        // on OBJECT_DELETE so by the time it can free this impl, the
        // audio-thread-iterated lists no longer reference it.
        if (ptr->parent != nullptr && ptr->connectedToParent.load(std::memory_order_acquire)) { // NOSONAR S8417: intentional acquire — pairs with release in implementationObject ctor handshake
          ptr->childrenToParent();
          ptr->parent->disconnect(ptr);
          ptr->connectedToParent.store(false, std::memory_order_release); // NOSONAR S8417: intentional release — publishes audio-thread disconnect before slow-pool delete
        }
        ptr->setStatus(OBJECT_DELETE);
        runDelete = true;
        continue;
      }
      previous = i;
      ++i;
    }
  }
}


YSE::CHANNEL::implementationObject * YSE::CHANNEL::managerObject::addImplementation(YSE::channel * head) {
  std::scoped_lock lk(implementationsMutex);
  implementations.emplace_front(head);
  return &implementations.front();
}

void YSE::CHANNEL::managerObject::setup(implementationObject * impl) {
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
  implementationObject * drained;
  while (toLoadInbox.try_pop(drained)) { (void)drained; }
  toLoad.clear();
  inUse.clear();
  {
    std::scoped_lock lk(implementationsMutex);
    // Each impl destructor nulls its interface's pimpl, clearing the persistent
    // master/named channels so the next System::init() re-creates them cleanly.
    implementations.clear();
  }
  runDelete = false;
}

YSE::channel & YSE::CHANNEL::managerObject::master() {
  return _master;
}

YSE::channel & YSE::CHANNEL::managerObject::FX() {
  return _fx;
}

YSE::channel & YSE::CHANNEL::managerObject::music() {
  return _music;
}

YSE::channel & YSE::CHANNEL::managerObject::ambient() {
  return _ambient;
}

YSE::channel & YSE::CHANNEL::managerObject::voice() {
  return _voice;
}

YSE::channel & YSE::CHANNEL::managerObject::gui() {
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

void YSE::CHANNEL::managerObject::setMaster(CHANNEL::implementationObject * impl) {
  impl->objectStatus = OBJECT_CREATED;
  impl->setup();
  DEVICE::Manager().setMaster(impl);
}

void YSE::CHANNEL::managerObject::setChannelConf(CHANNEL_TYPE type, Int outputs) {
  outputChannels = outputs;
  channelType = type;
}

void YSE::CHANNEL::managerObject::changeChannelConf() {
  delete[] outputAngles;
  outputAngles = new aFlt[outputChannels.load()];
  switch (channelType.load()) {
    case CT_AUTO: setAuto(outputChannels); break;
    case CT_MONO: setMono(); break;
    case CT_STEREO: setStereo(); break;
    case CT_QUAD: setQuad(); break;
    case CT_51: set51(); break;
    case CT_51SIDE: set51Side(); break;
    case CT_61:	set61(); break;
    case CT_71:	set71(); break;
    case CT_CUSTOM: break; // we've set number of outputs. CT_CUSTOM expects the positions will be 
                           // set later
  }

  REVERB::Manager().setOutputChannels(outputChannels);
  
  for (auto i = inUse.begin(); i != inUse.end(); i++) {
    (*i)->setup();
  }
}

void YSE::CHANNEL::managerObject::setAuto(Int count) {
  switch (count) {
  case	1: setMono(); break;
  case	2: setStereo(); break;
  case	4: setQuad(); break;
  case	5: set51(); break;
  case	6: set51(); break;
  case	7: set61(); break;
  case  8: set71(); break;
  default: setStereo(); break;
  }
}

void YSE::CHANNEL::managerObject::setMono() {
  outputAngles[0] = 0;
}

void YSE::CHANNEL::managerObject::setStereo() {
  outputAngles[0] = Pi / 180.0f * -90.0f;
  outputAngles[1] = Pi / 180.0f *  90.0f;
}

void YSE::CHANNEL::managerObject::setQuad() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f * -135.0f;
  outputAngles[3] = Pi / 180.0f *  135.0f;
}

void YSE::CHANNEL::managerObject::set51() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f *	   0.0f;
  outputAngles[3] = Pi / 180.0f * -135.0f;
  outputAngles[4] = Pi / 180.0f *	 135.0f;
}

void YSE::CHANNEL::managerObject::set51Side() {
  outputAngles[0] = Pi / 180.0f * -45.0f;
  outputAngles[1] = Pi / 180.0f *  45.0f;
  outputAngles[2] = Pi / 180.0f *		0.0f;
  outputAngles[3] = Pi / 180.0f * -90.0f;
  outputAngles[4] = Pi / 180.0f *  90.0f;
}

void YSE::CHANNEL::managerObject::set61() {
  outputAngles[0] = Pi / 180.0f * -45.0f;
  outputAngles[1] = Pi / 180.0f *  45.0f;
  outputAngles[2] = Pi / 180.0f *	  0.0f;
  outputAngles[3] = Pi / 180.0f * -90.0f;
  outputAngles[4] = Pi / 180.0f *  90.0f;
  outputAngles[5] = Pi / 180.0f * 180.0f;
}

void YSE::CHANNEL::managerObject::set71() {
  outputAngles[0] = Pi / 180.0f *  -45.0f;
  outputAngles[1] = Pi / 180.0f *   45.0f;
  outputAngles[2] = Pi / 180.0f *	   0.0f;
  outputAngles[3] = Pi / 180.0f *  -90.0f;
  outputAngles[4] = Pi / 180.0f *	  90.0f;
  outputAngles[5] = Pi / 180.0f * -135.0f;
  outputAngles[6] = Pi / 180.0f *  135.0f;
}

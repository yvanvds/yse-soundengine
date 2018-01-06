/*
  ==============================================================================

    thread.cpp
    Created: 1 Oct 2014 12:37:48pm
    Author:  yvan

  ==============================================================================
*/

#include "thread.h"
#include <assert.h>
#include "time.h"

YSE::INTERNAL::thread::thread()
: shouldExit(false) {
}

YSE::INTERNAL::thread::~thread()
{
  assert(!isRunning());
  stop();
}

void YSE::INTERNAL::thread::start() {
  if (!handle) {
    shouldExit = false;
    handle.reset(new std::thread(&thread::run, this));
  }
}

bool YSE::INTERNAL::thread::isRunning() const {
  return (handle) ? true : false;
}

void YSE::INTERNAL::thread::stop() {

  if (handle) {
    // you cannot stop a thread from within that thread!
    assert(handle->get_id() != std::this_thread::get_id());

    shouldExit = true;

    if (handle->joinable()) {
      handle->join();
      handle.reset();
    }
  }
}

bool YSE::INTERNAL::thread::threadShouldExit() const {
  return shouldExit;
}
#pragma once
#include "../pObject.h"
#include <cstddef>
#include <string>

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief A ``.s`` whose destination is chosen at runtime — ``.forward``
     *         (issue #485).
     *
     *  Max's ``forward``, whose one-line summary is "Send messages to a receive
     *  object": "Unlike the send object, the receive object to which forward
     *  sends its output can be changed dynamically with each message."
     *
     *  ### Why this exists next to ``.s``
     *
     *  ``.s`` publishes to a name fixed when the object is created. That is a
     *  structural fact in exactly the way a patch cord is: to reach a different
     *  receiver a patch has to be edited, which in this patcher means a
     *  GraphState swap (#226/#228) — a replacement object built on the control
     *  thread and published between blocks. ``.forward`` moves the same decision
     *  into a *message*: it costs one bounded copy into a buffer the object
     *  already owns, it takes effect on the next value through it, and the graph
     *  never changes shape at all.
     *
     *  Which is the point for the live-coding DSL the issue names, where the bus
     *  address is usually computed — ``voice`` plus an index, a name chosen by a
     *  ``.sel``, a destination that follows a counter. Writing that with ``.s``
     *  means one ``.s`` per possible destination behind a ``.gate``, so the set
     *  of reachable names is fixed at edit time by the object count. Here the set
     *  is whatever the patch can spell.
     *
     *  Everything downstream of the name is deliberately identical to ``.s``: the
     *  in-patcher ``PassData`` fan-out to matching ``.r`` nodes, the global-bus
     *  publish under ``"<patcherName>.<destination>"``, and the ``globalOnly``
     *  second argument that suppresses the first of those. A ``.forward`` whose
     *  destination never changes *is* a ``.s``, which is what makes it safe to
     *  reach for.
     *
     *  ### The destination has its own inlet
     *
     *  Max spells the change as a ``send <name>`` message into the object's one
     *  inlet, and can, because a Max box has one inlet and Max's parser can tell
     *  the *selector* ``send`` from the data around it. Here a list arrives as
     *  text, and reserving a first word out of that text would cost this object
     *  the one property it is named for: ``.forward`` forwards **whatever it is
     *  given**, and a patch that legitimately routes the list ``send voice2``
     *  through it would silently lose the message and quietly re-aim the object
     *  instead.
     *
     *  So the destination goes in its own inlet — the arrangement ``.router`` and
     *  ``.matrix`` reached from the same collision on this branch, and the one
     *  the patcher can afford because it is not bound to Max's box. Inlet 0 stays
     *  a pure data inlet with no reserved words in it; inlet 1 is the name and
     *  nothing but the name, so there is no message it cannot express either.
     *
     *  Inlet 1 accepts a **list** — the patcher's carrier for a symbol — and an
     *  **int**, spelled through ``WriteInt``, which is the patcher's only
     *  int-to-text writer and therefore the spelling ``.r 3`` was given by its own
     *  creation argument. The two agree, which is what makes a destination
     *  computed by a ``.counter`` reach the receiver a patch author typed. A
     *  **float** is declined: its spelling would agree with nothing, since no
     *  ``.r`` is named ``3.500000``. A **bang** is declined too — the
     *  ``.decode`` / ``.router`` discipline of not registering a handler with
     *  nothing to do, which also leaves ``inlet::GetAcceptedTypes()`` reporting
     *  the real contract.
     *
     *  Only the first whitespace-separated token of a list is taken. A name with
     *  a space in it can never match a ``.r``, because ``Parameters::Set``
     *  tokenises the creation argument the same way — so ``a b`` would name a
     *  receiver that cannot exist. Taking the first token is also what Max's
     *  ``send <name>`` does with its surplus arguments.
     *
     *  ### A destination is refused rather than truncated
     *
     *  ``NamedBus`` copies a published name into a fixed ``kNameCapacity``-byte
     *  slot and **truncates** anything longer. The in-patcher ``PassData`` path
     *  does not truncate. So a 70-character destination would address one
     *  receiver locally and a different, shorter name on the bus — one word
     *  meaning two places, and neither of them the one that was written.
     *
     *  A name longer than ``MAX_NAME_LENGTH`` is therefore rejected outright and
     *  the previous destination kept. On the inlet the rejection is silent, since
     *  that path may run on the audio thread and a log line allocates; on the
     *  creation argument, which is control-thread only, it is logged and the
     *  object starts with no destination at all.
     *
     *  The same limit is what lets the address buffer be sized once. ``.s``
     *  precomputes ``"<patcherName>.<dataName>"`` on the control thread because
     *  concatenating it per message would allocate on the audio path (issue
     *  #187); here the name half moves, so the *prefix* is what is precomputed,
     *  and the address is refilled into a buffer reserved for the longest
     *  destination this object will accept. Assigning into reserved capacity
     *  neither allocates nor frees — the discipline
     *  ``patcherImplementation::listScratch_`` already applies on the same path.
     *
     *  ### Nothing is resolved on the audio thread
     *
     *  Issue #485 asks for this to be verified against ``internal/namedBus.h``'s
     *  locking contract, and the contract answers it: ``NamedBus::publish`` on
     *  ``T_DSP`` does not touch ``subsMutex_`` at all. It copies the name into a
     *  pooled message and pushes it onto the calling thread's own lock-free SPSC
     *  queue; the name is matched against the subscriber map later, in
     *  ``drainPending()``, on the control thread. The ``std::shared_mutex`` the
     *  issue warns about is taken only by ``subscribe``, ``unsubscribe`` and the
     *  control-thread ``dispatch``.
     *
     *  So a *runtime* destination costs the audio thread nothing a fixed one
     *  would not: no resolution, no handle to hand across, and no lock. What is
     *  left is the address string, and that is preallocated. The in-patcher half
     *  is lock-free on ``T_DSP`` for the same reason ``.s``'s is —
     *  ``patcherImplementation::PassData`` dispatches straight against the pinned
     *  block snapshot.
     *
     *  ### An empty destination sends nothing
     *
     *  Not "sends to the empty name". A ``.forward`` with no creation argument
     *  and no name yet is an object that has not been told where to point, and
     *  publishing to ``"<patcherName>."`` would give it a real, reachable bus
     *  address that a second unconfigured ``.forward`` in a same-named patcher
     *  would share. Dropping is the only reading under which "not configured"
     *  stays distinguishable from "configured to send there".
     *
     *  ### Real-time behaviour
     *
     *  ``Calculate()`` does nothing: the object is driven by its inlets, and an
     *  emitting ``Calculate()`` would publish a value no patch sent — the rule
     *  ``.s`` and the whole routing family establish. No message path allocates,
     *  locks or blocks: the destination and the address are both refilled into
     *  reserved buffers, an int destination is spelled into a stack array, and
     *  the two delivery paths are ``.s``'s unchanged.
     */
    PATCHER_CLASS(gForward, YSE::OBJ::G_FORWARD)
    _NO_MESSAGES
    _NO_CALCULATE

    _BANG_IN(SetBangValue)
    _INT_IN(SetIntValue)
    _FLOAT_IN(SetFloatValue)
    _LIST_IN(SetListValue)

    _INT_IN(SetDestinationInt)
    _LIST_IN(SetDestinationList)

    _PARM_CLEAR
    _PARM_PARSE

    /**
     *  @brief Longest destination name the object will accept.
     *
     *  63 — ``INTERNAL::NamedBus::kNameCapacity``, asserted against it in the
     *  implementation so the two cannot drift apart. A longer name is refused
     *  rather than truncated, because the bus would truncate it and the
     *  in-patcher path would not, leaving one word addressing two different
     *  receivers.
     */
    static constexpr std::size_t MAX_NAME_LENGTH = 63;

    /**
     *  @brief Where the object currently points, or empty when it has not been
     *         told — in which case it sends nothing.
     */
    const std::string& Destination() const {
      return destination;
    }

    /**
     *  @brief Whether the in-patcher delivery is suppressed — the second
     *         creation argument, and ``.s``'s parameter of the same name.
     */
    int GlobalOnly() const {
      return globalOnly;
    }

    // Cache the "<patcherName>." address prefix the moment the parent is known,
    // so a publish never concatenates the patcher name on the message path.
    // Mirrors gSend::SetParent, which caches the whole address for the same
    // reason (issue #187).
    void SetParent(pObject* parent) override;

    // Recompute the cached prefix and address after a patcher rename. Mirrors
    // gSend::RefreshBusAddress(); called from patcherImplementation::SetName so
    // forwards keep matching the re-anchored receivers.
    void RefreshBusAddress();

  private:
    // Point at the first MAX_NAME_LENGTH-or-fewer characters at `text`, and
    // refill the cached address. Refuses — silently, this may be the audio
    // thread — a longer name, and any name that is only whitespace. Runs on
    // whichever thread sent the message, so it neither allocates nor logs:
    // both strings were reserved on the control thread.
    void SetDestination(const char* text, std::size_t length);

    // True when there is somewhere to send to at all: a parent to reach the
    // in-patcher receivers through, and a destination to name them by.
    bool Addressable() const {
      return parent != nullptr && !destination.empty();
    }

    // 0 = also fire the in-patcher PassData/PassBang path (default), 1 = publish
    // only to the global bus. The second creation argument, and gSend's
    // parameter of the same name and meaning (issue #122).
    int globalOnly = 0;

    // Where the object points now. The first creation argument, and thereafter
    // whatever inlet 1 was last given. Reserved for MAX_NAME_LENGTH at
    // construction so the inlet path refills it without allocating.
    std::string destination;

    // "<patcherName>.", precomputed on the control thread (SetParent /
    // RefreshBusAddress) and empty until a parent is assigned.
    std::string busPrefix;

    // busPrefix + destination — the address a publish uses. Refilled by
    // SetDestination on whichever thread set the name, into capacity reserved
    // for the longest destination the object will accept.
    std::string busAddress;
  };
} // namespace PATCHER
} // namespace YSE

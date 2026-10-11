#pragma once
#include "../dsp/buffer.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../headers/enums.hpp"
#include "pEnums.h"

namespace YSE {
  namespace PATCHER {

    class pObject;
    struct outlet;

    /**
     *  @brief Identity of the logical message event being dispatched right now
     *         on the calling thread — Max's "event", for ``.next`` (issue
     *         #471).
     *
     *  Max groups messages into *events*: one mouse click, one key press, one
     *  MIDI event, one tick of the scheduler. Everything a single stimulus
     *  causes — however many boxes it passes through, and however many messages
     *  those boxes emit — belongs to that one event. That is why Max's
     *  reference calls ``bang, bang`` from a message box, or two bangs from
     *  ``uzi``, "part of the same logical event", while two separate clicks on
     *  the same bang box are not.
     *
     *  This patcher has no scheduler and no logical clock, but it has the same
     *  *shape*: message delivery is synchronous and depth-first, so every
     *  message one stimulus causes is dispatched inside the call frame of the
     *  dispatch that started it. Counting that nesting recovers Max's grouping
     *  exactly — the outermost inlet dispatch on a thread opens an event, every
     *  dispatch nested inside it shares that event, and the next outermost
     *  dispatch opens a new one. So a ``.metro`` tick is one event, a
     *  ``.trigger``'s whole right-to-left fan-out is one event, and two calls
     *  from a host are two events.
     *
     *  Ids are unique across threads and never reused, so two unrelated events
     *  can never compare equal. **0 is never a live event**: it is what the
     *  function returns when no dispatch is in progress at all, which happens
     *  only when an object's message handler is called directly rather than
     *  through its inlet. A reader must treat 0 as "no event", not as an event
     *  that other messages could belong to.
     *
     *  RT-safe on every path — see the definition in inlet.cpp for the full
     *  argument. Reading it is a single thread-local load.
     */
    std::uint64_t CurrentMessageEvent();

    /**
     *  @brief RAII dispatch frame for a delivery that does not enter through an
     *         ``inlet::Set*`` call — the deferred-message scheduler's drain
     *         (issue #628).
     *
     *  The outermost scope opened on a thread starts a new logical event;
     *  nested scopes share it. This is exactly the frame the inlet setters open
     *  around their handlers, exported so the scheduler can hand a deferred
     *  delivery a proper event id: without it, each message a deferred send
     *  causes would open its *own* event at the first inlet it reaches, and a
     *  multi-outlet release (``.bondo``) would read as several unrelated
     *  stimuli to a downstream ``.next``.
     *
     *  RT-safe on every path — the same thread-local counter argument as
     *  CurrentMessageEvent() above; see inlet.cpp.
     */
    class messageEventScope {
    public:
      messageEventScope();
      ~messageEventScope();
      messageEventScope(const messageEventScope&) = delete;
      messageEventScope& operator=(const messageEventScope&) = delete;
    };

    typedef std::function<void(float, int, THREAD)> floatFunc;
    typedef std::function<void(int, int, THREAD)> intFunc;
    typedef std::function<void(int, THREAD)> voidFunc;
    typedef std::function<void(const std::string&, int, THREAD)> listFunc;
    typedef std::function<void(DSP::buffer* buffer, int, THREAD)> bufferFunc;

    /**
     *  @brief One inlet of a patcher object.
     *
     *  Signal inlets follow three rules (issue #1036):
     *
     *  - **Fan-in sums.** Any number of signal cords may end on one inlet. One
     *    source is passed through untouched; several are summed into the
     *    inlet's own scratch buffer, and the object hears one buffer (one
     *    buffer handler call) once the last of them has arrived.
     *  - **An unconnected signal inlet reads as a constant.** ``Signal()``
     *    returns the scratch buffer filled with the fallback value the object
     *    passes in: by convention the last float sent to that inlet, else the
     *    creation argument, else 0. Objects therefore always have a buffer to
     *    work with, and an object without signal sources is a start point that
     *    runs every block.
     *  - **A connected signal wins.** A float sent to a signal inlet with a
     *    cord on it does not change the audio; the object stores it, and it
     *    becomes the constant the moment the last cord is removed.
     */
    struct inlet {
      inlet(pObject* obj, bool active, int position);
      ~inlet();
      // Inlets live in a std::vector that only grows in the owning object's
      // constructor, before anything points at them. The scratch buffer is
      // owned, so an inlet is moved there, never copied.
      inlet(inlet&& other) noexcept;
      inlet(const inlet&) = delete;
      inlet& operator=(const inlet&) = delete;
      inlet& operator=(inlet&&) = delete;

      void RegisterBang(voidFunc f);
      void RegisterFloat(floatFunc f);
      void RegisterInt(intFunc f);
      void RegisterList(listFunc f);
      void RegisterBuffer(bufferFunc f);
      // Marks this as a signal inlet without a buffer handler: the object reads
      // the input with Signal() when it computes (issue #1036). Allocates the
      // scratch buffer, so call it from the constructor.
      void RegisterSignal();

      void SetBang(THREAD thread);
      void SetInt(int value, THREAD thread);
      void SetFloat(float value, THREAD thread);
      void SetList(const std::string& value, THREAD thread);
      void SetBuffer(DSP::buffer* value, THREAD thread);

      void SetMessage(const std::string& message, THREAD thread, float value = 0.f);

      inline void ResetDSP() {
        dspArrived = 0;
        current = nullptr;
      } // call before parsing patcher objects
      // True while fewer signal buffers have arrived this block than the inlet
      // has signal sources.
      bool WaitingForDSP() const;
      bool HasActiveDSPConnection() const;
      bool AcceptsDSP() const;

      /**
       *  @brief This block's input on a signal inlet (issue #1036).
       *
       *  The buffer that arrived (the sum, with several sources) or, when
       *  nothing arrived because the inlet is unconnected, the inlet's scratch
       *  buffer filled with @p fallback at ``STANDARD_BUFFERSIZE`` samples.
       *  Never null. Audio thread; allocation-free. Only valid on a signal
       *  inlet (one with RegisterBuffer or RegisterSignal).
       */
      DSP::buffer& Signal(Flt fallback);

      /** @brief True when Signal() would return the fallback constant: no
       *  buffer arrived this block. Lets an object take a scalar fast path. */
      inline bool IsConstant() const {
        return current == nullptr;
      }

      bool Connect(outlet* out);
      void Disconnect(outlet* out);

      // The live (control-thread) incoming edges: the control-edge sources
      // and the DSP sources. Read by the structural-replacement rewiring
      // of a live SetParams re-parse (issue #234); control thread only.
      inline const std::vector<outlet*>& Sources() const {
        return connections;
      }
      inline const std::vector<outlet*>& DspSources() const {
        return dspConnections;
      }

      // Drop this inlet's incoming edges and remove it from the outlets that
      // fed it, so a deleted object leaves no dangling reference in the next
      // GraphState (issue #226). Mirrors the destructor's peer cleanup but
      // leaves the inlet allocated.
      void UnwireFromPeers();

      // Dense, lifetime-stable id assigned when the owning object is added to a
      // patcher; indexes GraphState::inletDspSources. -1 until assigned.
      inline int GraphId() const {
        return graphId;
      }
      inline void SetGraphId(int id) {
        graphId = id;
      }

      int GetObjectID();
      int GetPosition();

      // Documentation surface. Populated in object constructors via the
      // INLET_DOC macro; consumed by the test_doc_coverage doctest and by
      // future binding-side metadata generators (issue #105). RT-cold: never
      // touched on the audio thread.
      void SetDoc(const std::string& label, const std::string& doc, const std::string& range);
      const std::string& GetDocLabel() const {
        return docLabel;
      }
      const std::string& GetDocDescription() const {
        return docDescription;
      }
      const std::string& GetRange() const {
        return docRange;
      }

      // Bitmask of InletType values indicating which message types this inlet
      // currently has handlers for. Reflects the REG_*_IN() calls in the
      // owning object's constructor.
      unsigned int GetAcceptedTypes() const;

    private:
      // How many signal sources this inlet has in the snapshot the calling
      // thread reads (see WaitingForDSP), or in the live wiring for a
      // standalone object.
      unsigned int DspSourceCount() const;

      pObject* obj;
      // Signal buffers received this block, and the one the object reads (the
      // single source's own buffer, or scratch holding the sum).
      unsigned int dspArrived;
      DSP::buffer* current;
      bool signal;
      bool active;
      int position;
      // See GraphId(). -1 = unassigned (standalone object, no patcher).
      int graphId = -1;

      intFunc onInt;
      voidFunc onBang;
      floatFunc onFloat;
      listFunc onList;
      bufferFunc onBuffer;

      std::vector<outlet*> dspConnections;
      std::vector<outlet*> connections;
      // Fan-in sum and unconnected constant. Allocated by RegisterBuffer /
      // RegisterSignal on the control thread, pre-sized to
      // STANDARD_BUFFERSIZE; null on a control-only inlet.
      std::unique_ptr<DSP::buffer> scratch;

      std::string docLabel;
      std::string docDescription;
      std::string docRange;
    };
  } // namespace PATCHER
} // namespace YSE

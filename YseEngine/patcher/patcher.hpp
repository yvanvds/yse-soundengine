#pragma once
#include "pHandle.hpp"

namespace YSE {
  class sound;

  /// @cond INTERNAL
  namespace PATCHER {
    class patcherImplementation;
  }
  namespace DSP {
    class patcherInsert;
  }
  /// @endcond

  /**
   *  @brief Base class for receiving OSC-style messages from a patcher.
   *
   *  Subclass and override the ``Send`` overloads to forward patcher output
   *  to an external system (OSC client, game logic, telemetry, ...). Register
   *  the instance with ``patcher::SetOscHandler``.
   */
  class API oscHandler {
  public:
    /** @brief Called when the patcher emits a bare bang (address only). */
    virtual void Send(const std::string&) {}
    /** @brief Called when the patcher emits an integer value. */
    virtual void Send(const std::string&, int) {}
    /** @brief Called when the patcher emits a float value. */
    virtual void Send(const std::string&, float) {}
    /** @brief Called when the patcher emits a string value. */
    virtual void Send(const std::string&, const std::string&) {}
    virtual ~oscHandler() {}
  };

  /**
   *  @brief Modular DSP / event graph (Max/MSP-style patcher).
   *
   *  Programmatically build a node graph, connect inlets to outlets, and
   *  optionally save/load it as JSON. A ``patcher`` can be used directly via
   *  the ``PassBang`` / ``PassData`` entry points, or wrapped into a
   *  ``YSE::sound`` via ``YSE::sound::create(patcher&, ...)`` to use it as an
   *  audio source.
   *
   *  Object type strings (``"~sine"``, ``".+"``, ``"~lp"`` etc.) are defined
   *  in ``YSE::OBJ`` — see ``patcher/pObjectList.hpp``.
   *
   *  @see YSE::pHandle
   *  @see YSE::OBJ
   */
  class API patcher {
  public:
    patcher();
    ~patcher();

    /** @brief Initialise the patcher with ``mainOutputs`` audio outputs. */
    void create(int mainOutputs);

    /**
     *  @brief Set the patcher's name (used as the prefix on the global bus).
     *
     *  Every ``gSend`` inside this patcher publishes its value to the global
     *  ``NamedBus`` under ``"<patcherName>.<dataName>"`` (issue #122). Two
     *  patchers that share the same name route their sends/receives
     *  together; patchers with distinct names stay isolated even when their
     *  inner ``dataName`` values collide.
     *
     *  Calling this after creating ``gReceive`` objects transparently
     *  re-subscribes them under the new name. If ``create()`` has not yet
     *  been called the value is stashed and applied at ``create()`` time.
     *
     *  Default: an auto-generated identifier of the form
     *  ``"patcher_<N>"`` where ``N`` increments per process.
     */
    patcher& name(const std::string& n);

    /** @brief Current patcher name. See ``name(const std::string&)``. */
    const std::string& name() const;

    /**
     *  @brief Add an object to the patcher.
     *
     *  @param type Type identifier (see ``YSE::OBJ``).
     *  @param args Optional creation arguments, formatted as the object expects.
     *  @return Handle to the new object — owned by the patcher.
     */
    YSE::pHandle* CreateObject(const std::string& type, const std::string& args = std::string());

    /** @brief Remove an object from the patcher. */
    void DeleteObject(YSE::pHandle* obj);

    /** @brief Remove every object from the patcher. */
    void Clear();

    /** @brief Connect ``from``'s outlet to ``to``'s inlet. */
    void Connect(YSE::pHandle* from, int outlet, YSE::pHandle* to, int inlet);

    /** @brief Remove the connection from ``from``'s outlet to ``to``'s inlet. */
    void Disconnect(YSE::pHandle* from, int outlet, YSE::pHandle* to, int inlet);

    /**
     *  @brief Put ``obj`` inside the subpatcher ``container`` (issue #545).
     *
     *  ``container`` must be a ``"patcher"`` object in this patcher, or
     *  ``nullptr`` to move ``obj`` back out to the top level. A subpatcher
     *  cannot be placed inside itself or inside one of its own descendants.
     *  Anything else is refused with a log and no change.
     *
     *  Containment is addressing, not storage: a nested object is still an
     *  ordinary object in this patcher's one flat graph, and this call
     *  publishes nothing because it changes no edge and no pin. What it changes
     *  is what ``Connect``/``Disconnect`` mean when handed the subpatcher —
     *  inlet *N* of a subpatcher is the boundary object inside it whose index
     *  is *N*, a ``".inlet"`` for a message pin or a ``"~inlet"`` for a signal
     *  pin (issue #764) — and what ``DeleteObject`` takes with it.
     */
    void SetContainer(YSE::pHandle* obj, YSE::pHandle* container);

    /** @brief The subpatcher ``obj`` is inside, or ``nullptr`` at the top level. */
    YSE::pHandle* GetContainer(YSE::pHandle* obj);

    /**
     *  @brief How many inlets a subpatcher presents to its parent.
     *
     *  One past the highest index claimed by a boundary object among the
     *  subpatcher's contents, so a sparsely numbered boundary reports the range
     *  a parent can address rather than the number of boundary objects. 0 for a
     *  handle that is not a subpatcher.
     *
     *  Both rates count towards the one total: a subpatcher has one set of
     *  inlet pins, and a ``".inlet 0"`` beside a ``"~inlet 1"`` presents two
     *  inlets — a message pin and a signal pin — not one of each (issue #764).
     *
     *  A ``"patcher"`` object owns no pins of its own, so ``pHandle::GetInputs``
     *  answers 0 for one; this is the question to ask instead.
     */
    int SubpatcherInlets(YSE::pHandle* container);

    /** @brief How many outlets a subpatcher presents. See ``SubpatcherInlets``. */
    int SubpatcherOutlets(YSE::pHandle* container);

    /** @brief Whether ``type`` is a known object type identifier. */
    static bool IsValidObject(const char* type);

    /** @brief Serialise the current graph to JSON. */
    std::string DumpJSON();

    /** @brief Replace the current graph with the contents of a JSON dump. */
    void ParseJSON(const std::string& content);

    /** @brief Number of objects in the patcher.
     *  @note Most useful after ``ParseJSON`` to walk the loaded graph.
     */
    unsigned int Objects();

    /** @brief Object at position ``obj`` in the list (0-indexed). */
    YSE::pHandle* GetHandleFromList(unsigned int obj);

    /** @brief Object whose ID is ``obj``. */
    YSE::pHandle* GetHandleFromID(unsigned int obj);

    /** @brief Send a bang to the named ``receive`` object. Returns false if no such receiver
     * exists. */
    bool PassBang(const std::string& to);

    /** @brief Send an integer to the named ``receive`` object. */
    bool PassData(int value, const std::string& to);

    /** @brief Send a float to the named ``receive`` object. */
    bool PassData(float value, const std::string& to);

    /** @brief Send a string to the named ``receive`` object. */
    bool PassData(const std::string& value, const std::string& to);

    /** @brief Install an OSC handler for outgoing messages. */
    void SetOscHandler(oscHandler* handle);

  private:
    PATCHER::patcherImplementation* pimpl;
    // Name stashed when name() is called before create(). Empty otherwise —
    // the impl's auto-generated default is the source of truth once create()
    // has run.
    std::string pendingName;
    friend class YSE::sound;
    // The insert adapter reaches through to pimpl to render the graph in place
    // (issue #167).
    friend class YSE::DSP::patcherInsert;
  };

} // namespace YSE
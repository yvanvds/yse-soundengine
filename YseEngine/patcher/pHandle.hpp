#pragma once
#include "../headers/defines.hpp"
#include <string>
#include "../headers/enums.hpp"
#include "../utils/vector.hpp"

namespace YSE {
  /// @cond INTERNAL
  namespace PATCHER {
    class pObject;
    class patcherImplementation;
  } // namespace PATCHER
  /// @endcond

  /**
   *  @brief Handle to a single object inside a ``patcher``.
   *
   *  Returned by ``patcher::CreateObject`` and the various lookup methods.
   *  Use it to feed external data into the object's inlets, inspect its
   *  outlets, read or write GUI metadata, and walk the loaded graph after
   *  ``patcher::ParseJSON``.
   *
   *  The patcher owns the underlying object — do not delete the handle
   *  directly; call ``patcher::DeleteObject`` instead.
   */
  class API pHandle {
  public:
    /** @brief Construct a handle around a patcher object. Engine-internal. */
    pHandle(PATCHER::pObject* obj);

    /** @brief Type identifier of the underlying object (see ``YSE::OBJ``). */
    const char* Type() const;

    /** @brief Send a bang to inlet ``inlet``. */
    void SetBang(unsigned int inlet);

    /** @brief Send an integer to inlet ``inlet``. */
    void SetIntData(unsigned int inlet, int value);

    /** @brief Send a float to inlet ``inlet``. */
    void SetFloatData(unsigned int inlet, float value);

    /** @brief Send a string or list to inlet ``inlet``. */
    void SetListData(unsigned int inlet, const std::string& value);

    /** @brief Reconfigure the object using a new argument string. */
    void SetParams(const std::string& args);

    /** @brief Read a GUI property by key (used by patcher editors). */
    std::string GetGuiProperty(const std::string& key);

    /** @brief Write a GUI property. */
    void SetGuiProperty(const std::string& key, const std::string& value);

    /** @brief Number of inlets on this object. */
    int GetInputs();

    /** @brief Number of outlets on this object. */
    int GetOutputs();

    /** @brief Whether inlet ``inlet`` accepts an audio signal. */
    bool IsDSPInput(unsigned int inlet);

    /** @brief Data type produced by outlet ``pin``. */
    YSE::OUT_TYPE OutputDataType(unsigned int pin);

    /** @brief Display name of this object (set in the patcher source). */
    std::string GetName();

    /** @brief Original creation argument string. */
    std::string GetParams();

    /** @brief Storage ID, unique among the owning patcher's live objects.
     *
     *  Assigned by the patcher when the object is created and stable for the
     *  object's lifetime. This is the number ``patcher::DumpJSON`` writes for
     *  the object and that ``GetConnectionTarget`` reports for edges pointing
     *  at it. IDs are *not* unique across patchers — two patchers each number
     *  their own objects from 0 (issue #730) — so use
     *  ``patcher::GetHandleFromID`` on the patcher the object belongs to.
     *
     *  IDs are kept dense: the patcher issues the smallest number no live
     *  object holds, so a deleted object's ID goes to the next object created
     *  and a long editing session does not push a small patch's numbering up
     *  (issue #733). The consequence for a caller is that an ID names an object
     *  only while that object is alive — an ID cached across a delete may
     *  resolve to the object that inherited the number rather than to nothing.
     *  Hold the ``pHandle*`` for a lasting reference; use the ID for storage
     *  and for talking about a patch.
     */
    unsigned int GetID();

    /** @brief Number of connections leaving outlet ``outlet``.
     *
     *  0 when the object has no such outlet — ``SetParams`` can shrink an
     *  object's outlet count, so an outlet number cached across a re-parse may
     *  no longer name anything (issue #737). Compare against ``GetOutputs()``
     *  to tell an absent outlet from an unconnected one.
     */
    unsigned int GetConnections(unsigned int outlet);

    /** @brief ID of the target object of one connection from outlet ``outlet``.
     *
     *  ``UINT_MAX`` when there is no such outlet or no such connection on it.
     *  Not 0: IDs start at 0 (issue #730), so 0 is a real target.
     */
    unsigned int GetConnectionTarget(unsigned int outlet, unsigned int connection);

    /** @brief Inlet on the target that this connection reaches.
     *
     *  ``UINT_MAX`` (``pObject::kNoInletIndex``) when there is no such outlet
     *  or no such connection on it. Not 0: inlet 0 is the leftmost inlet and
     *  the one most edges arrive at (issue #736).
     */
    unsigned int GetConnectionTargetInlet(unsigned int outlet, unsigned int connection);

    /** @brief Current GUI state of this object, as one string.
     *
     *  For a scalar control (``.slider``, ``.t``, ``.i``, ...) this is the
     *  value; for a structured one (``.rslider``, ``.multislider``,
     *  ``.matrixctrl``) it is every cell, space separated, in index order.
     *  Empty for an object with no GUI state.
     *
     *  Host thread, and it may be *destructive*: ``.b`` reports the press it
     *  is clearing. Poll an object through this call or through
     *  ``GetGuiValueAt`` once per frame, never both. See the GUI value
     *  protocol block in ``patcher/pObject.h`` for the full contract.
     */
    std::string GetGuiValue();

    /** @brief How many cells this object's GUI state has. 1 for a scalar control. */
    unsigned int GetGuiValueCount();

    /** @brief One cell of the GUI state, or "" past the end.
     *
     *  Cell 0 of a scalar control is exactly what ``GetGuiValue`` returns.
     *  Cells are sampled one call at a time and the read is not atomic across
     *  them — use ``GetGuiValue`` when a coherent snapshot matters.
     */
    std::string GetGuiValueAt(unsigned int index);

    /** @brief Whether this object accepts its own GUI state back on inlet 0.
     *
     *  True means the round trip holds: ``SetListData(0, GetGuiValue())``
     *  restores the state that was read, and ``SetListData(0, "set <index>
     *  <value>")`` writes a single cell. That is how a preset is restored —
     *  as ordinary messages on the control thread, never by writing another
     *  object's state directly.
     *
     *  The round trip is the half a preset needs and it holds unconditionally.
     *  The cell form has one carve-out, stated in the protocol block: a
     *  one-cell control whose cell is free text (``.textedit``) takes its whole
     *  inlet verbatim, because a text cell can hold the word ``set``. With one
     *  cell the two forms are the same write anyway.
     *
     *  False for the scalar controls that predate the protocol: their inlet 0
     *  takes an int or a float, not the display string ``GetGuiValue``
     *  produces.
     */
    bool GuiValueIsSettable();

  private:
    PATCHER::pObject* object;
    friend class YSE::PATCHER::patcherImplementation;
  };

} // namespace YSE

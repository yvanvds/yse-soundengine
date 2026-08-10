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

    /** @brief Current GUI display value for objects that have one (sliders, toggles, ...). */
    std::string GetGuiValue();

  private:
    PATCHER::pObject* object;
    friend class YSE::PATCHER::patcherImplementation;
  };

} // namespace YSE

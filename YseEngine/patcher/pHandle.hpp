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
  }
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
    pHandle(PATCHER::pObject * obj);

    /** @brief Type identifier of the underlying object (see ``YSE::OBJ``). */
    const char * Type() const;

    /** @brief Send a bang to inlet ``inlet``. */
    void SetBang(unsigned int inlet);

    /** @brief Send an integer to inlet ``inlet``. */
    void SetIntData(unsigned int inlet, int value);

    /** @brief Send a float to inlet ``inlet``. */
    void SetFloatData(unsigned int inlet, float value);

    /** @brief Send a string or list to inlet ``inlet``. */
    void SetListData(unsigned int inlet, const std::string & value);

    /** @brief Reconfigure the object using a new argument string. */
    void SetParams(const std::string & args);

    /** @brief Read a GUI property by key (used by patcher editors). */
    std::string GetGuiProperty(const std::string & key);

    /** @brief Write a GUI property. */
    void SetGuiProperty(const std::string & key, const std::string & value);

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

    /** @brief Unique patcher-assigned ID. */
    unsigned int GetID();

    /** @brief Number of connections leaving outlet ``outlet``. */
    unsigned int GetConnections(unsigned int outlet);

    /** @brief ID of the target object of one connection from outlet ``outlet``. */
    unsigned int GetConnectionTarget(unsigned int outlet, unsigned int connection);

    /** @brief Inlet on the target that this connection reaches. */
    unsigned int GetConnectionTargetInlet(unsigned int outlet, unsigned int connection);

    /** @brief Current GUI display value for objects that have one (sliders, toggles, ...). */
    std::string GetGuiValue();
  private:
    PATCHER::pObject * object;
    friend class YSE::PATCHER::patcherImplementation;
  };


}

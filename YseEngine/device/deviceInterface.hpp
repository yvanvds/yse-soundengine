/*
  ==============================================================================

    device.hpp
    Created: 10 Apr 2014 2:43:14pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICEINTERFACE_HPP_INCLUDED
#define DEVICEINTERFACE_HPP_INCLUDED
#include <string>
#include <vector>
#include "../classes.hpp"
#include "../yse.hpp"

namespace YSE {

  /**
   *  @brief Read-only descriptor of an audio device on the host system.
   *
   *  Enumerated and populated by the engine — applications retrieve instances
   *  through ``System().getDevices()`` (or the indexed
   *  ``getNumDevices`` / ``getDevice`` pair when libYSE is linked dynamically).
   *  Use the device to inspect its name, host (ASIO / WASAPI / ALSA / ...),
   *  available channel layouts, sample rates, and buffer sizes; then pass a
   *  ``deviceSetup`` derived from that info to ``System().openDevice``.
   *
   *  @note Do not construct directly — instances handed out by the engine are
   *        the only valid ones.
   *
   *  @see YSE::deviceSetup
   *  @see YSE::system::openDevice
   */
  class API device {
  public:
    /** @brief Default constructor used internally by the device enumerator. */
    device();

    /** @brief Set the device name. Used by the engine when populating the device list. */
    device& setName(const std::string& name);

    /** @brief Device name as reported by the host. */
    const std::string& getName() const;

    /** @brief Set the host (driver) name. Used internally. */
    device& setTypeName(const std::string& name);

    /** @brief Host (driver) name, e.g. ``ASIO``, ``WASAPI``, ``ALSA``, ``JACK``. */
    const std::string& getTypeName() const;

    /** @brief Internal: append an input channel name to the descriptor. */
    device& addInputChannelName(const std::string& name);

    /** @brief Internal: append an output channel name to the descriptor. */
    device& addOutputChannelName(const std::string& name);

    /** @brief Internal: register an available sample rate. */
    device& addAvailableSampleRate(double sr);

    /** @brief Internal: register an available buffer size. */
    device& addAvailableBufferSize(int bs);

    /** @brief All output channel names.
     *  @note Not usable across DLL boundaries — use the indexed
     *        ``getNumOutputChannelNames`` / ``getOutputChannelName`` pair when
     *        libYSE is linked dynamically.
     */
    const std::vector<std::string>& getOutputChannelNames() const;

    /** @brief All input channel names. Same DLL caveat as ``getOutputChannelNames``. */
    const std::vector<std::string>& getInputChannelNames() const;

    /** @brief All available sample rates. Same DLL caveat. */
    const std::vector<double>& getAvailableSampleRates() const;

    /** @brief All available buffer sizes. Same DLL caveat. */
    const std::vector<int>& getAvailableBufferSizes() const;

    /** @brief Number of output channels. */
    unsigned int getNumOutputChannelNames() const;

    /** @brief Name of output channel ``nr``.
     *  @throws std::out_of_range if ``nr >= getNumOutputChannelNames()``.
     */
    const std::string& getOutputChannelName(unsigned int nr) const;

    /** @brief Number of input channels. */
    unsigned int getNumInputChannelNames() const;

    /** @brief Name of input channel ``nr``.
     *  @throws std::out_of_range if ``nr >= getNumInputChannelNames()``.
     */
    const std::string& getInputChannelName(unsigned int nr) const;

    /** @brief Number of supported sample rates. */
    unsigned int getNumAvailableSampleRates() const;

    /** @brief Supported sample rate at index ``nr``.
     *  @throws std::out_of_range if ``nr >= getNumAvailableSampleRates()``.
     */
    double getAvailableSampleRate(unsigned int nr) const;

    /** @brief Number of supported buffer sizes. */
    unsigned int getNumAvailableBufferSizes() const;

    /** @brief Supported buffer size at index ``nr``.
     *  @throws std::out_of_range if ``nr >= getNumAvailableBufferSizes()``.
     */
    int getAvailableBufferSize(unsigned int nr) const;

    /** @brief Set the default buffer size to use when this device is opened. */
    device& setDefaultBufferSize(int value);

    /** @brief Default buffer size for this device, or ``0`` when the host does
     *         not advertise one.
     *
     *  ``0`` means *unspecified*, not *zero frames*: fed to
     *  ``deviceSetup::setBufferSize`` it asks the backend to pick the size
     *  itself (PortAudio's ``paFramesPerBufferUnspecified``). The PortAudio
     *  enumerator leaves it at ``0`` for every device it reports, because
     *  ``PaDeviceInfo`` carries no equivalent field and inventing a number
     *  would be worse than saying nothing (issue #569). Read
     *  ``System().getActiveBufferSize()`` after the device is open for the
     *  size actually negotiated.
     */
    int getDefaultBufferSize() const;

    /** @brief Set the reported output latency in samples. */
    device& setOutputLatency(int value);

    /** @brief Reported output latency in samples. */
    int getOutputLatency() const;

    /** @brief Set the reported input latency in samples. */
    device& setInputLatency(int value);

    /** @brief Reported input latency in samples. */
    int getInputLatency() const;

    /** @brief Set the host-assigned ID for this device. */
    device& setID(int value);

    /** @brief Host-assigned ID for this device. */
    int getID() const;

  private:
    std::string name;
    std::string typeName;

    std::vector<std::string> outputChannelNames;
    std::vector<std::string> inputChannelNames;
    std::vector<double> sampleRates;
    std::vector<int> bufferSizes;

    // Initialised here rather than in the constructor's mem-init list (issue
    // #569). The four scalars sat uninitialised from 2014 until #565 precisely
    // because the declarations and their initialisation lived in two different
    // files; a default member initialiser keeps them in one place and covers
    // any constructor added later, not just the one in deviceInterface.cpp.
    //
    // ID stays 0 rather than the -1 sentinel #569 floated: PortAudio's
    // paNoDevice is -1, and managerObject::openDevice() feeds getID() straight
    // into Pa_GetDeviceInfo() and dereferences the result without a null check,
    // so a -1 default would turn an unpopulated descriptor from a wrong-device
    // open into a null dereference. Guarding that call is issue #661.
    int defaultBufferSize = 0;
    int inputLatency = 0, outputLatency = 0;
    int ID = 0;

    friend class DEVICE::managerObject;
  };

} // namespace YSE

#endif // DEVICE_HPP_INCLUDED

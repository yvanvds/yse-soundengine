#include "pAudioIO.h"
#include <cstring>
#include "../pListArgs.h"
#include "../pSelector.h"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;

namespace {

  // A channel number from a number: whole, 1 to MAX_CHANNEL, else 0 (none).
  int ChannelFrom(float number) {
    if (!(number >= 1.f) || !(number <= static_cast<float>(pAudioIO::MAX_CHANNEL))) return 0;
    return static_cast<int>(number);
  }

} // namespace

pAudioIO::pAudioIO(int channels) : pObject(true), defaultChannels(channels < 0 ? 0 : channels) {}

int pAudioIO::Route(unsigned int pin) const {
  if (pin >= routeCount) return -1;
  return routes[pin].load(std::memory_order_relaxed) - 1;
}

void pAudioIO::SetRoute(unsigned int pin, int channel) {
  if (pin >= routeCount) return;
  if (channel < 1 || channel > MAX_CHANNEL) channel = 0;
  routes[pin].store(channel, std::memory_order_relaxed);
}

bool pAudioIO::DspOn() const {
  if (parent == nullptr) return true;
  return static_cast<patcherImplementation*>(parent)->DspOn();
}

void pAudioIO::SetDspOn(bool on) {
  if (parent == nullptr) return;
  static_cast<patcherImplementation*>(parent)->SetDspOn(on);
}

bool pAudioIO::NextToken(const std::string& text, std::size_t from, std::size_t& begin,
                         std::size_t& end) {
  const char* chars = text.c_str();
  const std::size_t length = text.size();
  begin = from;
  while (begin < length && IsSelectorSeparator(chars[begin]))
    begin++;
  end = begin;
  while (end < length && !IsSelectorSeparator(chars[end]))
    end++;
  return end > begin;
}

bool pAudioIO::TokenIs(const std::string& text, std::size_t begin, std::size_t end,
                       const char* word) {
  const std::size_t length = std::strlen(word);
  return end - begin == length && std::memcmp(text.c_str() + begin, word, length) == 0;
}

void pAudioIO::HandleSwitchNumber(float value) {
  SetDspOn(value != 0.f);
}

bool pAudioIO::HandleSwitch(const std::string& value) {
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return false;
  // startwindow turns on this patcher, subpatchers included, which is all
  // `start` turns on here: the switch belongs to the patcher.
  if (TokenIs(value, begin, end, "start") || TokenIs(value, begin, end, "startwindow")) {
    SetDspOn(true);
    return true;
  }
  if (TokenIs(value, begin, end, "stop")) {
    SetDspOn(false);
    return true;
  }
  return false;
}

void pAudioIO::SetRoutesFrom(unsigned int firstPin, const std::string& text, std::size_t from) {
  std::size_t begin = 0;
  std::size_t end = from;
  unsigned int pin = firstPin;
  while (pin < routeCount && NextToken(text, end, begin, end)) {
    float number = 0.f;
    if (!ReadNumericToken(text.c_str() + begin, end - begin, number)) return;
    SetRoute(pin, ChannelFrom(number));
    pin++;
  }
}

void pAudioIO::ArgumentChannels(std::vector<int>& channels, bool singleIsCount) const {
  channels.clear();
  for (const std::string& token : creationArgs) {
    float number = 0.f;
    if (!ReadNumericToken(token, number)) continue;
    if (channels.size() >= static_cast<std::size_t>(MAX_PINS)) break;
    channels.push_back(ChannelFrom(number));
  }
  if (singleIsCount && channels.size() == 1 && channels[0] > 0) {
    const int count = channels[0] < MAX_PINS ? channels[0] : MAX_PINS;
    channels.clear();
    for (int i = 1; i <= count; i++)
      channels.push_back(i);
  }
  if (channels.empty()) {
    for (int i = 1; i <= defaultChannels && i <= MAX_PINS; i++)
      channels.push_back(i);
  }
}

void pAudioIO::BuildRoutes(const std::vector<int>& channels) {
  routeCount = static_cast<unsigned int>(channels.size());
  routes.reset(routeCount > 0 ? new std::atomic<int>[routeCount] : nullptr);
  for (unsigned int i = 0; i < routeCount; i++)
    routes[i].store(channels[i], std::memory_order_relaxed);
}

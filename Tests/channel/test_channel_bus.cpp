// Tests for YSE::channel named-bus addressing (issue #123, epic #119).
//
// A named channel subscribes to "channel.<name>.volume" on the global named
// bus. T_GUI publishes dispatch synchronously, so these tests publish then
// read getVolume() back without pumping an update tick. Engine-dependent cases
// guard on engineInit() (audio paused), matching the rest of the channel suite.

#include <doctest/doctest.h>

#include <string>

#include "channel/channelInterface.hpp"
#include "internal/namedBus.h"
#include "support/null_device.hpp"

namespace {
void publishFloat(const std::string& name, float v) {
    YSE::INTERNAL::Bus().publish(name, YSE::INTERNAL::BusValue{v}, YSE::T_GUI);
}
} // namespace

TEST_SUITE("channel") {

TEST_CASE("channel bus: volume publish mutes the channel") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel ch;
    ch.create("music_log", YSE::ChannelMaster());
    ch.name("music");

    CHECK(ch.getVolume() == doctest::Approx(1.0f)); // default
    publishFloat("channel.music.volume", 0.0f);
    CHECK(ch.getVolume() == doctest::Approx(0.0f));
}

TEST_CASE("channel bus: name() is independent of the create log name") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel ch;
    ch.create("LogLabel", YSE::ChannelMaster());
    ch.name("busname");

    // getName() still returns the create label, not the bus name.
    CHECK(std::string(ch.getName()) == "LogLabel");

    publishFloat("channel.busname.volume", 0.5f);
    CHECK(ch.getVolume() == doctest::Approx(0.5f));
}

TEST_CASE("channel bus: chainable name() returns the channel") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel ch;
    ch.create("chain", YSE::ChannelMaster());
    ch.name("chained").setVolume(0.5f);
    CHECK(ch.getVolume() == doctest::Approx(0.5f));
}

TEST_CASE("channel bus: anonymous channel is not addressable") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel ch;
    ch.create("anon", YSE::ChannelMaster());
    // No name(): publishing reaches no subscriber, volume stays at default.
    publishFloat("channel.anon.volume", 0.0f);
    CHECK(ch.getVolume() == doctest::Approx(1.0f));
}

TEST_CASE("channel bus: destroying a named channel unsubscribes and frees the name") {
    if (!TestHelpers::engineInit()) return;
    {
        YSE::channel ch;
        ch.create("temp", YSE::ChannelMaster());
        ch.name("temp_bus");
        publishFloat("channel.temp_bus.volume", 0.3f);
        CHECK(ch.getVolume() == doctest::Approx(0.3f));
    } // ~channel() unsubscribes here

    // Dead address: no crash, no effect.
    publishFloat("channel.temp_bus.volume", 0.9f);

    // Name reclaimable by a new channel.
    YSE::channel ch2;
    ch2.create("temp2", YSE::ChannelMaster());
    ch2.name("temp_bus");
    publishFloat("channel.temp_bus.volume", 0.1f);
    CHECK(ch2.getVolume() == doctest::Approx(0.1f));
}

TEST_CASE("channel bus: duplicate name is rejected and the first registration wins") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel a, b;
    a.create("a_log", YSE::ChannelMaster());
    b.create("b_log", YSE::ChannelMaster());

    a.name("cdup");
    b.name("cdup"); // rejected + logged; a keeps ownership

    publishFloat("channel.cdup.volume", 0.4f);
    CHECK(a.getVolume() == doctest::Approx(0.4f)); // first receives
    CHECK(b.getVolume() == doctest::Approx(1.0f)); // second never subscribed (default)
}

} // TEST_SUITE("channel")

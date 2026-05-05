// Tests for YSE channel system (YseEngine/channel/).
// Covers: channelMessage accessors, channel default state, manager lifecycle,
// default channel hierarchy, volume/virtual flag API, and custom channel creation.
//
// Engine-dependent tests guard on TestHelpers::engineInit().  If System().init()
// fails (e.g., no PortAudio host APIs at all), those cases return without
// failing any assertion, which doctest counts as a pass.  The engine state is
// initialised once and shared across all test cases in this process.

#include <doctest/doctest.h>
#include <string>
#include "channel/channelInterface.hpp"
#include "channel/channelMessage.h"
#include "support/null_device.hpp"

TEST_SUITE("channel") {

// ─── channelMessage: construction and field accessors ─────────────────────────

TEST_CASE("channelMessage: VOLUME message stores float value") {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::VOLUME;
    m.floatValue = 0.75f;
    CHECK(m.ID == YSE::CHANNEL::VOLUME);
    CHECK(m.floatValue == doctest::Approx(0.75f));
}

TEST_CASE("channelMessage: MOVE message stores pointer value") {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::MOVE;
    void* sentinel = &m;
    m.ptrValue = sentinel;
    CHECK(m.ID == YSE::CHANNEL::MOVE);
    CHECK(m.ptrValue == sentinel);
}

TEST_CASE("channelMessage: VIRTUAL message stores bool value") {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::VIRTUAL;
    m.boolValue = false;
    CHECK(m.ID == YSE::CHANNEL::VIRTUAL);
    CHECK(m.boolValue == false);
}

TEST_CASE("channelMessage: ATTACH_REVERB ID is distinct from other IDs") {
    YSE::CHANNEL::messageObject m;
    m.ID = YSE::CHANNEL::ATTACH_REVERB;
    CHECK(m.ID == YSE::CHANNEL::ATTACH_REVERB);
    CHECK(m.ID != YSE::CHANNEL::VOLUME);
    CHECK(m.ID != YSE::CHANNEL::MOVE);
    CHECK(m.ID != YSE::CHANNEL::VIRTUAL);
}

TEST_CASE("channelMessage: float union field round-trips correctly") {
    YSE::CHANNEL::messageObject m;
    m.floatValue = 0.123f;
    CHECK(m.floatValue == doctest::Approx(0.123f));
}

TEST_CASE("channelMessage: bool union field round-trips correctly") {
    YSE::CHANNEL::messageObject m;
    m.boolValue = true;
    CHECK(m.boolValue == true);
    m.boolValue = false;
    CHECK(m.boolValue == false);
}

// ─── Channel default state (no engine init required) ─────────────────────────

TEST_CASE("channel: default-constructed channel is not valid") {
    YSE::channel c;
    CHECK_FALSE(c.isValid());
}

TEST_CASE("channel: default volume is 1.0") {
    YSE::channel c;
    CHECK(c.getVolume() == doctest::Approx(1.0f));
}

TEST_CASE("channel: default allowVirtual is true") {
    YSE::channel c;
    CHECK(c.getVirtual() == true);
}

// ─── Manager lifecycle and default channel handles ────────────────────────────

TEST_CASE("channelManager: master channel is valid after engine init") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelMaster().isValid());
}

TEST_CASE("channelManager: master channel name is 'Master channel'") {
    if (!TestHelpers::engineInit()) return;
    CHECK(std::string(YSE::ChannelMaster().getName()) == "Master channel");
}

TEST_CASE("channelManager: all five default leaf channels are valid after engine init") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelFX().isValid());
    CHECK(YSE::ChannelMusic().isValid());
    CHECK(YSE::ChannelAmbient().isValid());
    CHECK(YSE::ChannelVoice().isValid());
    CHECK(YSE::ChannelGui().isValid());
}

TEST_CASE("channelManager: default leaf channel names are correct") {
    if (!TestHelpers::engineInit()) return;
    CHECK(std::string(YSE::ChannelFX().getName())      == "fxChannel");
    CHECK(std::string(YSE::ChannelMusic().getName())   == "musicChannel");
    CHECK(std::string(YSE::ChannelAmbient().getName()) == "ambientChannel");
    CHECK(std::string(YSE::ChannelVoice().getName())   == "voiceChannel");
    CHECK(std::string(YSE::ChannelGui().getName())     == "guiChannel");
}

// ─── Volume API ───────────────────────────────────────────────────────────────

TEST_CASE("channel: master channel default volume is 1.0 after init") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelMaster().getVolume() == doctest::Approx(1.0f));
}

TEST_CASE("channel: setVolume / getVolume round-trip on leaf channel") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelFX().setVolume(0.5f);
    CHECK(YSE::ChannelFX().getVolume() == doctest::Approx(0.5f));
    YSE::ChannelFX().setVolume(1.0f); // restore
}

TEST_CASE("channel: setVolume clamps values above 1.0 to 1.0") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelMusic().setVolume(2.0f);
    CHECK(YSE::ChannelMusic().getVolume() == doctest::Approx(1.0f));
}

TEST_CASE("channel: setVolume clamps negative values to 0.0") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelAmbient().setVolume(-0.5f);
    CHECK(YSE::ChannelAmbient().getVolume() == doctest::Approx(0.0f));
    YSE::ChannelAmbient().setVolume(1.0f); // restore
}

TEST_CASE("channel: setVolume to 0.0 is accepted") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelVoice().setVolume(0.0f);
    CHECK(YSE::ChannelVoice().getVolume() == doctest::Approx(0.0f));
    YSE::ChannelVoice().setVolume(1.0f); // restore
}

// ─── Virtual flag API ─────────────────────────────────────────────────────────

TEST_CASE("channel: default virtual flag is true on all default channels") {
    if (!TestHelpers::engineInit()) return;
    CHECK(YSE::ChannelFX().getVirtual()      == true);
    CHECK(YSE::ChannelMusic().getVirtual()   == true);
    CHECK(YSE::ChannelAmbient().getVirtual() == true);
    CHECK(YSE::ChannelVoice().getVirtual()   == true);
    CHECK(YSE::ChannelGui().getVirtual()     == true);
}

TEST_CASE("channel: setVirtual false / getVirtual round-trip") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelGui().setVirtual(false);
    CHECK(YSE::ChannelGui().getVirtual() == false);
    YSE::ChannelGui().setVirtual(true); // restore
}

TEST_CASE("channel: setVirtual true restores flag after setting false") {
    if (!TestHelpers::engineInit()) return;
    YSE::ChannelGui().setVirtual(false);
    YSE::ChannelGui().setVirtual(true);
    CHECK(YSE::ChannelGui().getVirtual() == true);
}

// ─── Custom channel creation ──────────────────────────────────────────────────

TEST_CASE("channel: user-created channel becomes valid after create()") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel custom;
    CHECK_FALSE(custom.isValid());
    custom.create("testChannel", YSE::ChannelFX());
    CHECK(custom.isValid());
    CHECK(std::string(custom.getName()) == "testChannel");
}

TEST_CASE("channel: user-created channel default volume is 1.0") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel custom;
    custom.create("volTestChannel", YSE::ChannelMusic());
    CHECK(custom.getVolume() == doctest::Approx(1.0f));
}

TEST_CASE("channel: user-created channel default allowVirtual is true") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel custom;
    custom.create("virtTestChannel", YSE::ChannelAmbient());
    CHECK(custom.getVirtual() == true);
}

TEST_CASE("channel: user-created channel accepts setVolume before dsp update") {
    if (!TestHelpers::engineInit()) return;
    YSE::channel custom;
    custom.create("setVolChannel", YSE::ChannelVoice());
    custom.setVolume(0.3f);
    CHECK(custom.getVolume() == doctest::Approx(0.3f));
}

} // TEST_SUITE("channel")

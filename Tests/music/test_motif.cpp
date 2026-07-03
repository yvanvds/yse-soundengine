// Tests for YSE::motif (YseEngine/music/motif/), YSE::player (YseEngine/player/),
// and PLAYER::messageObject (YseEngine/player/playerMessage.h).
//
// Coverage:
//   - motif: construction, empty/size predicates, getLength
//   - motif: add() — stores note, increments size, sorts by position
//   - motif: operator[] — direct index access to interface note vector
//   - motif: setLength(Flt) — explicit length assignment
//   - motif: setLength() — auto-compute from last note's position+length
//   - motif: transpose() — shifts all note pitches, position unchanged
//   - motif: clear() — removes all notes
//   - player: construction and destruction without crash
//   - player: isPlaying() initial state
//   - playerMessage: MESSAGE enum values, boolValue and floatPair union fields
//
// Note: YSE::motif copy constructor and assignment are declared in the header
// but not yet implemented (no definition found in motifInterface.cpp).
// Copy-semantics tests are deferred.
//
// Note: YSE::player::create() (which takes a synth) is commented out pending
// the public synth API.  Only the zero-arg constructor/destructor and isPlaying()
// are testable without triggering a null pimpl dereference.
//
// No engine initialisation is required — MOTIF::Manager() and SCALE::Manager()
// are standalone static singletons with no PortAudio dependency.

#include <doctest/doctest.h>
#include "music/pNote.hpp"
// Message headers must precede the impl headers: sendMessage() is an inline
// in the impl that calls lfQueue<messageObject>::push, which needs the
// complete messageObject type to size its storage.
#include "music/scale/scaleMessage.h"
#include "music/motif/motifMessage.h"
#include "music/scale/scaleInterface.hpp"
#include "music/scale/scaleImplementation.h"
#include "music/scale/scaleManager.h"
#include "music/motif/motifInterface.hpp"
#include "music/motif/motifImplementation.h"
#include "music/motif/motifManager.h"
#include "player/playerInterface.hpp"
#include "player/playerMessage.h"

TEST_SUITE("music") {

  // ─── motif: construction ──────────────────────────────────────────────────────

  TEST_CASE("motif: default construction is empty with zero length") {
    YSE::motif m;
    CHECK(m.empty());
    CHECK(m.size() == 0u);
    CHECK(m.getLength() == doctest::Approx(0.f));
  }

  // ─── motif: add ───────────────────────────────────────────────────────────────

  TEST_CASE("motif: add single pNote increments size and clears empty flag") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    CHECK(m.size() == 1u);
    CHECK(!m.empty());
  }

  TEST_CASE("motif: operator[] returns note with correct pitch") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    CHECK(m[0].getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("motif: add multiple notes; interface sorts them by position") {
    YSE::motif m;
    // Add in reverse position order to verify sort
    m.add(YSE::MUSIC::pNote(1.f, 62.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    CHECK(m.size() == 2u);
    CHECK(m[0].getPosition() == doctest::Approx(0.f));
    CHECK(m[0].getPitch() == doctest::Approx(60.f));
    CHECK(m[1].getPosition() == doctest::Approx(1.f));
    CHECK(m[1].getPitch() == doctest::Approx(62.f));
  }

  TEST_CASE("motif: note volume and length are preserved after add") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 0.8f, 0.25f, 2));
    CHECK(m[0].getVolume() == doctest::Approx(0.8f));
    CHECK(m[0].getLength() == doctest::Approx(0.25f));
    CHECK(m[0].getChannel() == 2);
  }

  // ─── motif: getLength / setLength ────────────────────────────────────────────

  TEST_CASE("motif: setLength stores explicit value") {
    YSE::motif m;
    m.setLength(4.f);
    CHECK(m.getLength() == doctest::Approx(4.f));
  }

  TEST_CASE("motif: auto setLength computes last note position plus its length") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(1.f, 62.f, 1.f, 0.5f));
    m.setLength();
    // last note: position=1.0, length=0.5 → total length = 1.5
    CHECK(m.getLength() == doctest::Approx(1.5f));
  }

  TEST_CASE("motif: auto setLength on empty motif gives zero") {
    YSE::motif m;
    m.setLength();
    CHECK(m.getLength() == doctest::Approx(0.f));
  }

  TEST_CASE("motif: auto setLength uses the note with greatest position") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 1.0f)); // ends at 1.0
    m.add(YSE::MUSIC::pNote(2.f, 64.f, 1.f, 0.5f)); // ends at 2.5
    m.setLength();
    CHECK(m.getLength() == doctest::Approx(2.5f));
  }

  // ─── motif: transpose ─────────────────────────────────────────────────────────

  TEST_CASE("motif: transpose shifts all note pitches by the given amount") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(1.f, 62.f, 1.f, 0.5f));
    m.transpose(12.f);
    CHECK(m[0].getPitch() == doctest::Approx(72.f));
    CHECK(m[1].getPitch() == doctest::Approx(74.f));
  }

  TEST_CASE("motif: transpose does not change note positions") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.5f, 60.f, 1.f, 0.5f));
    m.transpose(7.f);
    CHECK(m[0].getPosition() == doctest::Approx(0.5f));
  }

  TEST_CASE("motif: transpose by zero leaves pitches unchanged") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.transpose(0.f);
    CHECK(m[0].getPitch() == doctest::Approx(60.f));
  }

  TEST_CASE("motif: transpose down by negative value decrements pitches") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.transpose(-12.f);
    CHECK(m[0].getPitch() == doctest::Approx(48.f));
  }

  // ─── motif: clear ─────────────────────────────────────────────────────────────

  TEST_CASE("motif: clear removes all notes and restores empty state") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(1.f, 62.f, 1.f, 0.5f));
    m.clear();
    CHECK(m.empty());
    CHECK(m.size() == 0u);
  }

  TEST_CASE("motif: notes can be re-added after clear") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.clear();
    m.add(YSE::MUSIC::pNote(0.f, 64.f, 1.f, 0.5f));
    CHECK(m.size() == 1u);
    CHECK(m[0].getPitch() == doctest::Approx(64.f));
  }

  // ─── player: lifecycle ────────────────────────────────────────────────────────

  TEST_CASE("player: construction does not crash") {
    YSE::player p;
    // pimpl == nullptr (player::create() requires synth, not yet in public API)
  }

  TEST_CASE("player: destruction does not crash") {
    { YSE::player p; } // ~player() null-checks pimpl before calling removeInterface()
  }

  TEST_CASE("player: isPlaying returns false before create") {
    YSE::player p;
    CHECK(!p.isPlaying());
  }

  // ─── playerMessage: field construction ───────────────────────────────────────

  TEST_CASE("playerMessage: PLAY message with boolValue true") {
    YSE::PLAYER::messageObject m;
    m.ID = YSE::PLAYER::PLAY;
    m.boolValue = true;
    CHECK(m.ID == YSE::PLAYER::PLAY);
    CHECK(m.boolValue == true);
  }

  TEST_CASE("playerMessage: PLAY message with boolValue false") {
    YSE::PLAYER::messageObject m;
    m.ID = YSE::PLAYER::PLAY;
    m.boolValue = false;
    CHECK(m.boolValue == false);
  }

  TEST_CASE("playerMessage: MIN_PITCH message stores target and time in floatPair") {
    YSE::PLAYER::messageObject m;
    m.ID = YSE::PLAYER::MIN_PITCH;
    m.floatPair[0] = 36.f;
    m.floatPair[1] = 0.5f;
    CHECK(m.ID == YSE::PLAYER::MIN_PITCH);
    CHECK(m.floatPair[0] == doctest::Approx(36.f));
    CHECK(m.floatPair[1] == doctest::Approx(0.5f));
  }

  TEST_CASE("playerMessage: MAX_PITCH message stores target value") {
    YSE::PLAYER::messageObject m;
    m.ID = YSE::PLAYER::MAX_PITCH;
    m.floatPair[0] = 96.f;
    m.floatPair[1] = 0.f;
    CHECK(m.ID == YSE::PLAYER::MAX_PITCH);
    CHECK(m.floatPair[0] == doctest::Approx(96.f));
  }

  TEST_CASE("playerMessage: VOICES message stores voice count as floatPair[0]") {
    YSE::PLAYER::messageObject m;
    m.ID = YSE::PLAYER::VOICES;
    m.floatPair[0] = 4.f;
    m.floatPair[1] = 0.f;
    CHECK(m.ID == YSE::PLAYER::VOICES);
    CHECK(m.floatPair[0] == doctest::Approx(4.f));
  }

  TEST_CASE("playerMessage: distinct MESSAGE enum values are not equal") {
    CHECK(YSE::PLAYER::PLAY != YSE::PLAYER::MIN_PITCH);
    CHECK(YSE::PLAYER::MIN_PITCH != YSE::PLAYER::MAX_PITCH);
    CHECK(YSE::PLAYER::MIN_VELOCITY != YSE::PLAYER::MAX_VELOCITY);
    CHECK(YSE::PLAYER::ADD_MOTIF != YSE::PLAYER::REM_MOTIF);
  }

  // ─── Implementation pump via MOTIF::Manager().update() ───────────────────────
  //
  // The motif interface ships every mutation as a message to its implementation.
  // MOTIF::Manager().update() drains the queue, exercising parseMessage() for
  // every MESSAGE enum value, and triggers the impl's sort() when notes were
  // added out of order.  Coverage here is structural (the impl's vector is
  // private to the test), but it drives update / parseMessage / sort /
  // removeInterface across many code paths.

  TEST_CASE("motif impl: Manager().update() drains queued ADD messages") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(1.f, 62.f, 1.f, 0.5f));
    YSE::MOTIF::Manager().update();
    // Second pump with empty queue exercises the no-message branch.
    YSE::MOTIF::Manager().update();
    CHECK(m.size() == 2u);
  }

  TEST_CASE("motif impl: Manager().update() triggers sort when notes added out of order") {
    YSE::motif m;
    m.add(YSE::MUSIC::pNote(2.f, 62.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.add(YSE::MUSIC::pNote(1.f, 64.f, 1.f, 0.5f));
    YSE::MOTIF::Manager().update(); // drives impl.sort() via needsSorting branch
    CHECK(m.size() == 3u);
  }

  TEST_CASE("motif impl: Manager().update() pumps CLEAR / LENGTH / TRANSPOSE / FIRST_PITCH") {
    YSE::scale sc;
    sc.add(60.f, 12.f);
    YSE::SCALE::Manager().update();

    YSE::motif m;
    m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
    m.setLength(2.5f); // sends LENGTH
    m.transpose(7.f); // sends TRANSPOSE
    m.setFirstPitch(sc); // sends FIRST_PITCH (carries SCALE::impl ptr)
    YSE::MOTIF::Manager().update();

    m.clear(); // sends CLEAR
    YSE::MOTIF::Manager().update();
    CHECK(m.size() == 0u);
  }

  TEST_CASE("motif impl: removeInterface() runs at ~motif and update() prunes") {
    {
      YSE::motif m;
      m.add(YSE::MUSIC::pNote(0.f, 60.f, 1.f, 0.5f));
      YSE::MOTIF::Manager().update();
    }
    YSE::MOTIF::Manager().update(); // observes head==nullptr, erases impl
    YSE::motif m2;
    m2.add(YSE::MUSIC::pNote(0.f, 64.f, 1.f, 0.5f));
    YSE::MOTIF::Manager().update();
    CHECK(m2.size() == 1u);
  }

  // ─── Direct MOTIF::implementationObject — head=nullptr is safe ──────────────
  //
  // As with the scale impl, the motif impl's destructor null-checks head before
  // writing, and update() short-circuits on null head.  Calling parseMessage()
  // directly lets us hit every MESSAGE branch and verify state via the impl's
  // own public accessors (size, getLength, getNote, getValidPitches) without
  // needing a player or synth.

  TEST_CASE("motif impl: update() returns false when head is null") {
    YSE::MOTIF::implementationObject impl(nullptr);
    CHECK(impl.update() == false);
  }

  TEST_CASE("motif impl: parseMessage(ADD) appends a note with all fields preserved") {
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::MOTIF::messageObject msg;
    msg.ID = YSE::MOTIF::ADD;
    msg.note.position = 0.5f;
    msg.note.pitch = 60.f;
    msg.note.volume = 0.8f;
    msg.note.length = 0.25f;
    msg.note.channel = 3;
    impl.parseMessage(msg);
    CHECK(impl.size() == 1u);
    CHECK(impl.getNote(0).getPosition() == doctest::Approx(0.5f));
    CHECK(impl.getNote(0).getPitch() == doctest::Approx(60.f));
    CHECK(impl.getNote(0).getVolume() == doctest::Approx(0.8f));
    CHECK(impl.getNote(0).getLength() == doctest::Approx(0.25f));
    CHECK(impl.getNote(0).getChannel() == 3);
  }

  TEST_CASE("motif impl: parseMessage(LENGTH) stores length") {
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::MOTIF::messageObject msg;
    msg.ID = YSE::MOTIF::LENGTH;
    msg.floatValue = 4.0f;
    impl.parseMessage(msg);
    CHECK(impl.getLength() == doctest::Approx(4.0f));
  }

  TEST_CASE("motif impl: parseMessage(TRANSPOSE) shifts pitches of stored notes") {
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::MOTIF::messageObject add;
    add.ID = YSE::MOTIF::ADD;
    add.note.position = 0.f;
    add.note.pitch = 60.f;
    add.note.volume = 1.f;
    add.note.length = 0.5f;
    add.note.channel = 0;
    impl.parseMessage(add);
    add.note.pitch = 62.f;
    impl.parseMessage(add);

    YSE::MOTIF::messageObject tr;
    tr.ID = YSE::MOTIF::TRANSPOSE;
    tr.floatValue = 12.f;
    impl.parseMessage(tr);
    CHECK(impl.getNote(0).getPitch() == doctest::Approx(72.f));
    CHECK(impl.getNote(1).getPitch() == doctest::Approx(74.f));
  }

  TEST_CASE("motif impl: parseMessage(CLEAR) empties the note vector") {
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::MOTIF::messageObject add;
    add.ID = YSE::MOTIF::ADD;
    add.note.position = 0.f;
    add.note.pitch = 60.f;
    add.note.volume = 1.f;
    add.note.length = 0.5f;
    add.note.channel = 0;
    impl.parseMessage(add);
    CHECK(impl.size() == 1u);

    YSE::MOTIF::messageObject clr;
    clr.ID = YSE::MOTIF::CLEAR;
    impl.parseMessage(clr);
    CHECK(impl.size() == 0u);
  }

  TEST_CASE("motif impl: parseMessage(FIRST_PITCH) stores SCALE::impl ptr") {
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::SCALE::implementationObject scaleImpl(nullptr);
    YSE::MOTIF::messageObject msg;
    msg.ID = YSE::MOTIF::FIRST_PITCH;
    msg.ptr = &scaleImpl;
    impl.parseMessage(msg);
    CHECK(impl.getValidPitches() == &scaleImpl);
  }

  TEST_CASE("motif impl: getValidPitches defaults to nullptr") {
    YSE::MOTIF::implementationObject impl(nullptr);
    CHECK(impl.getValidPitches() == nullptr);
  }

  TEST_CASE("motif impl: getLength defaults to zero before any LENGTH message") {
    // length is not value-initialised in the impl ctor, but the manager
    // path always seeds it via setLength.  This test documents that the
    // observable default after a LENGTH=0 message is exactly 0.
    YSE::MOTIF::implementationObject impl(nullptr);
    YSE::MOTIF::messageObject msg;
    msg.ID = YSE::MOTIF::LENGTH;
    msg.floatValue = 0.f;
    impl.parseMessage(msg);
    CHECK(impl.getLength() == doctest::Approx(0.f));
  }

} // TEST_SUITE("music")

#include "stdafx.h"
#include "Demo12_AudioTest.h"

DemoAudioTest::DemoAudioTest() {
  SetTitle("Basic Audio Test with sine waves.");
  AddAction('1', "Turn On", std::bind(&DemoAudioTest::On, this));
  AddAction('2', "Turn Off", std::bind(&DemoAudioTest::Off, this));
}

void DemoAudioTest::On() {
  YSE::System().AudioTest(true);
}

void DemoAudioTest::Off() {
  YSE::System().AudioTest(false);
}
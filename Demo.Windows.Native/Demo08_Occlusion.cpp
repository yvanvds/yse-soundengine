#include "stdafx.h"
#include "Demo08_Occlusion.h"


Flt OccludeValue = 0;

Flt OcclusionFunction(const YSE::Pos& pos, const YSE::Pos& listener) {
  // for simplicity's sake, we just check with a global var here.
  // In reality you should do a raycast here to check these positions with
  // the physx or bullet implementation in your game.
  return OccludeValue;
}


DemoOcclusion::DemoOcclusion()
{
  SetTitle("Occlusion Demo");
  AddAction('1', "Increase Occlusion", std::bind(&DemoOcclusion::OcclusionInc, this));
  AddAction('2', "Decrease Occlusion", std::bind(&DemoOcclusion::OcclusionDec, this));

  OccludeValue = 0;

  YSE::System().occlusionCallback(OcclusionFunction);
  sound.create("..\\TestResources\\pulse1.ogg", nullptr, true).occlusion(true);
  sound.play();
}


DemoOcclusion::~DemoOcclusion()
{
  YSE::System().occlusionCallback(nullptr);
}

void DemoOcclusion::ExplainDemo()
{
  std::cout << "This is a basic implementation of the sound occlusion callback." << std::endl;
}

void DemoOcclusion::OcclusionInc()
{
  OccludeValue += 0.1f;
}

void DemoOcclusion::OcclusionDec()
{
  OccludeValue -= 0.1f;
}

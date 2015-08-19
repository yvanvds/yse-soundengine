Sound Occlusion
===============

The sound occlusion implementation in YSE is mainly targeted at game developers, although it might be of some use to sound artists too.

Most game developers (if working with some sort of spacial representation) already have a simplified version of their virtual environment by means of a PhysX environment (or Bullets on Mac). These libraries offer functions for collision detection which are also usable for sound occlusion.

To implement sound occlusion with YSE, you can provide a callback function. YSE will call this function to check if a sound should be occluded or not. Partial occlusion is also possible:

~~~~{.cpp}
float occlusionTest(const YSE::Vec \& source, const YSE::Vec \& listener) {
  // this code is made up. It won't work but is supposed to give you an
  // idea of how to implement an occlusion test function.
  object * found = FirstObjectBetween(source, listener);
  if(found != nullptr) {
    return found->density();
  }
  return 0;
}	

// enable occlusion globally
YSE::System().occlusionCallback(occlusionTest);

// turn occlusion on for the sounds you'd like to be affected
YSE::sound mySound;
mySound.create("afile.wav").setOcclusion(true);
~~~~

The code in the callback itself depends on what you would like to do, but it would probably be a raycast between the first and the second position. If any objects are in that line, the sound should be occluded. If you assign different materials to your objects, you can easily decide how much of the sound gets through.
	


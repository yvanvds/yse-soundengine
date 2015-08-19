3D versus 2D
============

Absolute Positions (3D)
=======================

By default, YSE will place a sound in a virtual 3D field. It's output will be rendered to one or more speakers in a stereo or surround set-up. This is calculated internally, so you only have to update a sound's position to move it to a new spot.

Another way to change the position of all sounds in the stereo field is by moving the Listener object. By default, the Listener object is at position 0x0y0z, pointing forward. You can change the listener's position and orientation. 

When you use YSE in a 3D game environment, the usual approach is to update the listener's position on every update. YSE will automatically calculate the current velocity of the listener and, when sounds have Doppler enabled, apply a pitch shift to the sound accordingly.

~~~~{.cpp}
YSE::Listener().SetPosition(YSE::Vec(0,0,0));
YSE::Listener().SetRotation(YSE::Vec(0,0,0));
~~~~

Relative Positions (2D)
=======================
What if you do not want to position your audio in 3D? YSE is a bit different compared to other sound engines in that EVERY sound has a position. But this position can be absolute (typical 3D) or relative to the listener. A sound has an absolute position by default. To make it relative:

~~~~{.cpp}
mySound.setRelative(true);
~~~~

A relative sound at position 0 will always be at the position of the listener. In YSE, it is the closest you get to a '2D sound'. This has the extra advantage that mono sounds in a 2D environment can still be panned out quite easily by using their position. For example, if you'd want a sound a bit to the left of the user:

~~~~{.cpp}
mySound.setRelative(true).setPosition(YSE::Vec(-1,0,0));
~~~~

Multichannel sound
==================

Multichannel sounds can be used just the same way as mono sounds. They can be at an absolute or a relative position. The function `YSE::SOUND::interfaceObject::setSpread()` controls the angle, i.e. how much space there is between the channels in the virtual environment. 

For example, if you play a stereo sound, with a spread of 90 degrees, and positioned at 0, the left channel will sound at -45\% and the left channel will sound at 45\%.

If you play a 4 channel sound with the same spread of 90 degrees, the first channel will play at -45\%, the second at -15\%, the third at 15\% and the fourth at 45\%.

Multichannel sounds can also be at absolute positions. In this case the angle of the channels will scale according to the position and angle of the sound itself and the listener.

Directional sound (mono or multichannel) is not implemented yet, but will follow later.



Channels
========

Using and adding channels
=========================

When you create a sound and you do not specify a channel, it will be added to the master channel. A few other channels do exist by default:

~~~~{.cpp}
YSE::ChannelGui();
YSE::ChannelFX();
YSE::ChannelAmbient();
YSE::ChannelVoice();
YSE::ChannelMusic();
~~~~

You can also create your own channels, should you want to. If you do, the channel will be created as a child of the master channel, unless you specify another channel as the parent. Just like a sound, a channel is a typedef to `YSE::CHANNEL::interfaceObject`. This means it can also be constructed before `System().init()` is called. The `create()` function should be called after `System().init()`.

When a channel is removed, all its child channels and sounds are moved to its parent. This can be done without any interruption in the sound system, but if there is a large difference in output volumes of the channels, there might be a short click in the output sound. 

Note that sounds and channels can also be moved from one channel to another manually.

Performance
===========

Channels have several purposes. First, a few properties like volume can be adjusted to all sounds in the channel at once. Read the doxygen docs for more information about channel functions.

Another purpose is more hidden, but actually very important. When the audio callback function is called, it instructs all channels to calculate the audio they'd like to output at that moment. Every channel does this in its own thread. If the device you're using has multiple cores, as most devices do nowadays, YSE takes advantage of all available cores to calculate its audio.

With this in mind, it is crucial to spread out sounds over multiple channels for the best performance. (If you only use YSE to play a few sounds at a time, don't bother. It's mostly useful for 20 or more simultaneous sounds.)

Another thing for which channels are important, are reverb calculations. This topic is discussed in another chapter.
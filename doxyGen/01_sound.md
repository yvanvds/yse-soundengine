Play Some Sound
===============

Initialise the Engine    
=====================

Apart from setting up a sound output, there's not much you can do with YSE before initialising. If you don't need any custom output setup, you can simply use the System() functor to initialise:
~~~~{.cpp}
YSE::System().init();
~~~~
Once you've started the sound system, its update function should be called frequently. For responsive audio, this should be about 10 to 20 times a second. If you're using YSE together with a game engine, a good place would be inside the game update loop. If your program doesn't have such a loop, you should consider a timer of some sort.
~~~~{.cpp}
YSE::System().update();		
~~~~
When you're about to shutdown your program, call the close function to properly stop audio output:
~~~~{.cpp}
YSE::System().close();
~~~~
The close function also ensures that internal threads are stopped properly. It's really important to call this before exiting your program.

Create a sound
==============

A sound object can be created from the class `YSE::sound`, which is actually a typedef for `YSE::SOUND::interfaceObject`. Constructing this object actually doesn't do much. This has the advantage that sound objects can be directly included inside classes or even at a global level, without using pointer constructions. Once the sound system is initialised, you can 'create' the sound, which means that a sound file or generator is loaded in memory.

Only after creating a sound, it can be played. Some properties of the sound can be passed to the create function, but behaviour can be altered later on.

~~~~{.cpp}
// example of simple sound playing

// A sound can exist before initialising because it is just an interface.
YSE::sound mySound;
  
// Before actually creating the sound, the system MUST be initialised.
YSE::System().init();

// Now the sound can be created and played
mySound.create("sound.ogg").play();
  
// or create on a subchannel, looping:
mySound.create("sound.ogg", YSE::System().channelFX(), true);
mySound.play();	
~~~~

Most set functions in yse can be chained. So
~~~~{.cpp}
mySound.setRelative(true);
mySound.setDoppler(false);
mySound.setPosition(Vec(1,1,1));
mySound.play();	
~~~~
is equal to:
~~~~{.cpp}
mySound.setRelative(true).setDoppler(false).setPosition(Vec(1,1,1)).play();	
~~~~
Sound objects are very flexible. I won't go over all features in here. Please refer to the doxygen manual for more information.

About loading sound files
=========================

So what happens when you instruct a sound object to create itself? You pass the name of a sound file to the sound object, but what happens then? 

Internally, YSE will check whether or not this file is already in memory. If it is, an extra pointer to this file will be created. You do not have to worry about the same file being loaded twice. 

Memory is also released when no longer needed. In reality, it the last pointer to a file is destructed, YSE will keep the file in memory for about 30 seconds. If after that period no new sound object has requested access to the sound file, it will be released from memory.

When a file is requested and not in memory, it will be loaded in a separate thread. It can take a short while to load the file, depending on how large it is and how fast your device is. Your program can just continue while YSE loads the file in memory. You can even define properties for this sound object, or instruct it to start playing. These instructions will internally be delayed until the file is done loading.

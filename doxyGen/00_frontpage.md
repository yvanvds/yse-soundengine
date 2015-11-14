Getting Started {#mainpage}
===============

The binary distribution
-----------------------
The YSE website includes a binary distribution with dynamic libraries for Windows and Android. (Other platforms should
be included soon.) Just add the include folder to your project and link to the library. Note that C++11 must be used to
build a project that is linked to YSE.

You should only need to include the main header file:
~~~~{.cpp}
#include "yse.hpp"
~~~~
The source distribution
-----------------------
Todo

Important Concepts
==================

Functors
--------

Some classes in YSE are probably needed in every program using the engine. I've decided to implement them as 'functors'. Internally, a functor is something like this:

~~~~{.cpp}
YSE::system & YSE::System() {
  static system s;
  return s;
}
~~~~

You should not try to create your own object from the system class. YSE uses this functor internally as well. We think this approach has some advantages:

- Code fragments are easier to read because the names for base objects (i.e. functors) are always the same.
- Contrary to global objects, the object in a functor is only created when first called.
- You don't have to keep track whether or not an object is created. You can just use it whenever needed. It might be created already, internally, or it will be created when you're trying to use it. Both will work the same.
- Because the functor returns a reference to the object, it can be used just like a real object, except for the braces.

Other functor objects are:

~~~~{.cpp}
YSE::System();
YSE::Listener();
YSE::Reverb();
YSE::Log();
YSE::IO();

YSE::ChannelMaster();
YSE::ChannelGui();
YSE::ChannelFX();
YSE::ChannelMusic();
YSE::ChannelAmbient();
YSE::ChannelVoice();
~~~~

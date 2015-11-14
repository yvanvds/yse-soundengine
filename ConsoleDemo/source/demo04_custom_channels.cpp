#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef YSE_WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif

/* Channels

  In YSE, Channels are groups of sounds that can be modified together. For example, changing the volume
  of a channel will impact all sounds in the channel. But it is also possible to link DSP filters to 
  channels, or asign reverb. 

  Channels can also be members of other channels. So you could build a more complex tree of channels and
  sounds. If you remove a custom made channel, all sounds will be moved to the parent channel. Moving sounds
  between channels is posible, but there might be glitches if the channel's gain level is very different.

  Very important:
  Every channel renders in its own thread. This means YSE will scale very well, as long as you don't assign
  all sounds to the same output channel. On the other hand, too much channels will also decrease performance
  because of all the thread handling...
  */


// normally you wouldn't use pointers for this, but this 
// demonstrates what happens if you delete a channel object
YSE::channel  * customChannel = NULL;
YSE::sound kick, pulse;

int main() {
  YSE::System().init();

  // create a custom channel and add it to the main channel
  customChannel = new YSE::channel;
  customChannel->create("myChannel", YSE::ChannelMaster());

  // add a sound to your custom channel
  kick.create("kick.ogg", customChannel, true);

  // add a sound to the music channel
  pulse.create("pulse1.ogg", &YSE::ChannelMusic(), true);

  std::cout << "Sounds are mixed in channels. Every channel is linked to the global channel. Custom channels can be created. If you delete a channel, the sounds in that channel move to the parent channel." << std::endl;
  std::cout << "Use q/a to change the globalChannel volume up and down." << std::endl;
  std::cout << "Use w/s to change the customChannel volume up and down." << std::endl;
  std::cout << "Use e/d to change the musicChannel volume up and down." << std::endl;
  std::cout << "Use r to delete the customChannel." << std::endl;
  std::cout << "...or 0 to exit." << std::endl;

  kick.play();
  pulse.play();

  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch (ch) {
      case 'q': YSE::ChannelMaster().setVolume(YSE::ChannelMaster().getVolume() + 0.1); break;
      case 'a': YSE::ChannelMaster().setVolume(YSE::ChannelMaster().getVolume() - 0.1); break;

      case 'w': if (customChannel != NULL) customChannel->setVolume(customChannel->getVolume() + 0.1); break;
      case 's': if (customChannel != NULL) customChannel->setVolume(customChannel->getVolume() - 0.1); break;
      case 'e': YSE::ChannelMusic().setVolume(YSE::ChannelMusic().getVolume() + 0.1); break;
      case 'd': YSE::ChannelMusic().setVolume(YSE::ChannelMusic().getVolume() - 0.1); break;
      case 'r': {
                  if (customChannel != NULL) {
                    delete customChannel;
                    customChannel = NULL;
                    std::cout << "The custom channel is deleted. All sounds and subchannels are automatically moved to the parent channel." << std::endl;
                  }
                  break;
                }

      case '0': goto exit;
      }
    }

    YSE::System().sleep(100);
    YSE::System().update();
  }

exit:
  YSE::System().close();
  return 0;
}
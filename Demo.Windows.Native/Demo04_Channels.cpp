#include "stdafx.h"
#include "Demo04_Channels.h"

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

DemoChannels::DemoChannels() : customChannel(new YSE::channel)
{
	customChannel->create("myChannel", YSE::ChannelMaster());

	// add a sound to your custom channel
	kick.create("..\\TestResources\\kick.ogg", customChannel, true).play();

	// add a sound to the music channel
	pulse.create("..\\TestResources\\pulse1.ogg", &YSE::ChannelMusic(), true).play();

	SetTitle("Custom Channels");
	AddAction('q', "Increase Volume (Master Channel)", std::bind(&DemoChannels::MasterIncVol, this));
	AddAction('a', "Decrease Volume (Master Channel)", std::bind(&DemoChannels::MasterDecVol, this));

	AddAction('w', "Increase Volume (Custom Channel)", std::bind(&DemoChannels::CustomIncVol, this));
	AddAction('s', "Decrease Volume (Custom Channel)", std::bind(&DemoChannels::CustomDecVol, this));

	AddAction('e', "Increase Volume (Music Channel)", std::bind(&DemoChannels::MusicIncVol, this));
	AddAction('d', "Decrease Volume (Music Channel)", std::bind(&DemoChannels::MusicDecVol, this));

	AddAction('r', "Delete Custom Channel", std::bind(&DemoChannels::CustomDelete, this));
}

DemoChannels::~DemoChannels()
{
	CustomDelete();
}

void DemoChannels::ExplainDemo()
{
	std::cout << "Sounds are mixed in channels. Every channel is linked to the global channel. Custom channels can be created. If you delete a channel, the sounds in that channel move to the parent channel." << std::endl;
}

void DemoChannels::MasterIncVol()
{
	YSE::ChannelMaster().setVolume(YSE::ChannelMaster().getVolume() + 0.1f);
}

void DemoChannels::MasterDecVol()
{
	YSE::ChannelMaster().setVolume(YSE::ChannelMaster().getVolume() - 0.1f);
}

void DemoChannels::CustomIncVol()
{
	if (customChannel != nullptr) customChannel->setVolume(customChannel->getVolume() + 0.1f);
}

void DemoChannels::CustomDecVol()
{
	if (customChannel != nullptr) customChannel->setVolume(customChannel->getVolume() - 0.1f);
}

void DemoChannels::MusicIncVol()
{
	YSE::ChannelMusic().setVolume(YSE::ChannelMusic().getVolume() + 0.1f);
}

void DemoChannels::MusicDecVol()
{
	YSE::ChannelMusic().setVolume(YSE::ChannelMusic().getVolume() - 0.1f);
}

void DemoChannels::CustomDelete()
{
	if (customChannel != nullptr) {
		delete customChannel;
		customChannel = nullptr;
		std::cout << "The custom channel is deleted. All sounds and subchannels are automatically moved to the parent channel." << std::endl;
	}
}


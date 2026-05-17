/*
  ==============================================================================

	channelImplementation.cpp
	Created: 30 Jan 2014 4:21:26pm
	Author:  yvan

  ==============================================================================
*/

#include "../internalHeaders.h"

YSE::CHANNEL::implementationObject::implementationObject(channel* head)
	: head(head), newVolume(1.f), lastVolume(1.f), parent(nullptr), allowVirtual(true)
{
}

YSE::CHANNEL::implementationObject::~implementationObject() noexcept
{
	try
	{
		// exit the dsp thread for this channel
		join();

		// The primary disconnect path is on the audio thread, in
		// CHANNEL::Manager::update at the OBJECT_RELEASE→OBJECT_DELETE transition.
		// This guard ensures the slow-pool's destructor only touches
		// parent->children when no audio-thread disconnect has happened (i.e.
		// setup-failure path: channels that died before connect() ran).
		if (INTERNAL::Global().isActive())
		{
			if (parent != nullptr && connectedToParent.load(std::memory_order_acquire))
			{ // NOSONAR S8417: intentional acquire — pairs with release in doThisWhenReady() /
			  // Manager::update() for lock-free handshake
				parent->disconnect(this);
				childrenToParent();
			}
		}

		if (head.load() != nullptr)
		{
			head.load()->pimpl = nullptr;
		}
	}
	catch (...)
	{
		INTERNAL::LogImpl().emit(E_ERROR,
								 "CHANNEL::implementationObject destructor swallowed exception");
	}
}

Bool YSE::CHANNEL::implementationObject::connect(CHANNEL::implementationObject* ch)
{
	if (ch != this)
	{
		if (ch->parent != nullptr)
			ch->parent->disconnect(ch);
		ch->parent = this;
		children.push_front(ch);
		return true;
	}
	else
		ch->parent = nullptr;
	return false;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::CHANNEL::implementationObject* ch)
{
	children.remove(ch);
	return true;
}

Bool YSE::CHANNEL::implementationObject::connect(YSE::SOUND::implementationObject* s)
{
	if (s->parent != nullptr && s->parent != this)
	{
		s->parent->disconnect(s);
	}
	s->parent = this;
	sounds.push_front(s);
	return true;
}

Bool YSE::CHANNEL::implementationObject::disconnect(YSE::SOUND::implementationObject* s)
{
	sounds.remove(s);
	return true;
}

void YSE::CHANNEL::implementationObject::run()
{
	dsp();
}

void YSE::CHANNEL::implementationObject::dsp()
{
	// if no sounds or other channels are linked, we skip this channel
	if (children.empty() && sounds.empty())
		return;

	// clear channel buffer
	clearBuffers();

	// calculate child channels if there are any
	for (auto i = children.begin(); i != children.end(); ++i)
	{
		INTERNAL::Global().addFastJob(*i);
	}

	// calculate sounds in this channel
	for (auto i = sounds.begin(); i != sounds.end(); ++i)
	{
		if ((*i)->dsp())
		{
			(*i)->toChannels();
		}
	}

	REVERB::Manager().process(this);

	if (INTERNAL::UnderWaterEffect().channel() == this)
	{
		INTERNAL::UnderWaterEffect().apply(out);
	}

	// Publish pre-volume peak for VU consumers. Sized in lock-step with `out`
	// in setup()/resize(); the std::min guards against a resize race.
	const UInt n = (UInt)std::min(out.size(), lastPeakLinearPre.size());
	for (UInt i = 0; i < n; ++i)
	{
		lastPeakLinearPre[i].v.store(out[i].maxValue(), std::memory_order_release);
	}
}

void YSE::CHANNEL::implementationObject::removeInterface()
{
	head.store(nullptr);
}

void YSE::CHANNEL::implementationObject::buffersToParent()
{
	join();

	// call this recursively on all child channels
	for (auto i = children.begin(); i != children.end(); ++i)
	{
		(*i)->buffersToParent();
	}

	// apply channel volume
	adjustVolume();

	// Publish post-volume peak for VU consumers — measured after adjustVolume()
	// so the reading reflects what listeners actually hear from this channel.
	// The master channel also reaches this point, giving a true master VU.
	{
		const UInt n = (UInt)std::min(out.size(), lastPeakLinearPost.size());
		for (UInt i = 0; i < n; ++i)
		{
			lastPeakLinearPost[i].v.store(out[i].maxValue(), std::memory_order_release);
		}
	}

	// if this is the main channel, we're done here
	if (parent == nullptr)
		return;
	if (children.empty() && sounds.empty())
		return;

	// if not the main channel, add output to parent channel
	for (UInt i = 0; i < out.size(); ++i)
	{
		// parent size is not checked but should be ok because it's adjusted before calling this
		parent->out[i] += out[i];
	}
}

void YSE::CHANNEL::implementationObject::attachUnderWaterFX()
{
	INTERNAL::UnderWaterEffect().channel(this);
}

void YSE::CHANNEL::implementationObject::setup()
{
	if (objectStatus >= OBJECT_CREATED)
	{
		if (objectStatus == OBJECT_READY)
			return;

		const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
		out.resize(numOutputs);
		outConf.resize(numOutputs);
		lastPeakLinearPre.resize(numOutputs);
		lastPeakLinearPost.resize(numOutputs);
		for (UInt i = 0; i < numOutputs; i++)
		{
			outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
		}
		objectStatus = OBJECT_SETUP;
	}
}

void YSE::CHANNEL::implementationObject::resize(bool deep)
{
	const UInt numOutputs = CHANNEL::Manager().getNumberOfOutputs();
	out.resize(numOutputs);
	outConf.resize(numOutputs);
	lastPeakLinearPre.resize(numOutputs);
	lastPeakLinearPost.resize(numOutputs);
	for (UInt i = 0; i < numOutputs; i++)
	{
		outConf[i].angle = CHANNEL::Manager().getOutputAngle(i);
	}
	if (deep)
	{
		for (auto i = children.begin(); i != children.end(); ++i)
		{
			(*i)->resize(true);
		}

		for (auto i = sounds.begin(); i != sounds.end(); ++i)
		{
			(*i)->resize();
		}
	}
}

Bool YSE::CHANNEL::implementationObject::readyCheck()
{
	// this means we have don this check before and returned true back then.
	// the object is added to the list of inUse, but is probably not deleted just
	// yet. It will be deleted the next time the remove_if function runs (in objectManager)
	if (objectStatus == OBJECT_READY)
	{
		return false;
	}
	if (objectStatus == OBJECT_SETUP)
	{
		if (outConf.size() == CHANNEL::Manager().getNumberOfOutputs())
		{
			objectStatus = OBJECT_READY;
			return true;
		}
	}
	objectStatus = OBJECT_CREATED;
	return false;
}

void YSE::CHANNEL::implementationObject::doThisWhenReady()
{
	parent->connect(this);
	// Audio thread now owns the link into parent->children. Mark it so the
	// release path can disconnect us before the slow-pool deleteJob frees us.
	connectedToParent.store(
		true, std::memory_order_release); // NOSONAR S8417: intentional release — publishes the
										  // audio-thread connect to the dtor's acquire load
}

YSE::OBJECT_IMPLEMENTATION_STATE YSE::CHANNEL::implementationObject::getStatus()
{
	return objectStatus.load();
}

void YSE::CHANNEL::implementationObject::setStatus(YSE::OBJECT_IMPLEMENTATION_STATE value)
{
	objectStatus.store(value);
}

void YSE::CHANNEL::implementationObject::sync()
{
	if (head.load() == nullptr)
	{
		objectStatus = OBJECT_RELEASE;
		return;
	}

	messageObject message;
	while (messages.try_pop(message))
	{
		parseMessage(message);
	}
}

void YSE::CHANNEL::implementationObject::parseMessage(const messageObject& message)
{
	switch (message.ID)
	{
	case ATTACH_REVERB:
		REVERB::Manager().attachToChannel(this);
		break;
	case MOVE:
	{
		channel* ptr = (channel*)message.ptrValue;
		if (ptr != nullptr)
		{
			ptr->pimpl->connect(this);
		}
		break;
	}
	case VIRTUAL:
		allowVirtual = message.boolValue;
		break;
	case VOLUME:
		newVolume = message.floatValue;
		break;
	}
}

void YSE::CHANNEL::implementationObject::childrenToParent()
{
	// don't do this if there is no parent channel
	if (parent == nullptr)
		return;

	{
		auto i = children.begin();
		while (i != children.end())
		{
			parent->connect(*i);
			i = children.begin();
		}
	}

	{
		auto i = sounds.begin();
		while (i != sounds.end())
		{
			parent->connect(*i);
			i = sounds.begin();
		}
	}
}

void YSE::CHANNEL::implementationObject::clearBuffers()
{
	for (UInt i = 0; i < out.size(); ++i)
	{
		out[i] = 0.0f;
	}
}

void YSE::CHANNEL::implementationObject::adjustVolume()
{
	if (newVolume != lastVolume)
	{
		// new value, create a ramp
		Flt step = (newVolume - lastVolume) / STANDARD_BUFFERSIZE;

		for (UInt i = 0; i < out.size(); ++i)
		{
			Flt multiplier = lastVolume;
			Flt* ptr = out[i].getPtr();
			for (UInt j = 0; j < STANDARD_BUFFERSIZE; j++)
			{
				*ptr++ *= multiplier;
				multiplier += step;
			}
		}
		lastVolume = newVolume;
	}
	else
	{
		// same volume, just copy
		for (UInt i = 0; i < out.size(); ++i)
		{
			out[i] *= newVolume;
		}
	}
}

int YSE::CHANNEL::implementationObject::getNumOutputs() const
{
	return static_cast<int>(out.size());
}

float YSE::CHANNEL::implementationObject::getPeakLinearPre(int outputIdx) const
{
	if (outputIdx < 0 || static_cast<size_t>(outputIdx) >= lastPeakLinearPre.size())
		return 0.f;
	return lastPeakLinearPre[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPost(int outputIdx) const
{
	if (outputIdx < 0 || static_cast<size_t>(outputIdx) >= lastPeakLinearPost.size())
		return 0.f;
	return lastPeakLinearPost[outputIdx].v.load(std::memory_order_acquire);
}

float YSE::CHANNEL::implementationObject::getPeakLinearPreCombined() const
{
	float peak = 0.f;
	for (const auto& cell : lastPeakLinearPre)
	{
		const float v = cell.v.load(std::memory_order_acquire);
		if (v > peak)
			peak = v;
	}
	return peak;
}

float YSE::CHANNEL::implementationObject::getPeakLinearPostCombined() const
{
	float peak = 0.f;
	for (const auto& cell : lastPeakLinearPost)
	{
		const float v = cell.v.load(std::memory_order_acquire);
		if (v > peak)
			peak = v;
	}
	return peak;
}

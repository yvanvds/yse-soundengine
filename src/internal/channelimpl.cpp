#include "stdafx.h"
#include "internal/channelimpl.h"
#include "utils/misc.hpp"
#include "internal/internalObjects.h"
#include "utils/error.hpp"
#include "internal/listenerimpl.h"

namespace UnderWater {
  YSE::DSP::sample buffer;
  YSE::DSP::lowPass filter;
  YSE::DSP::sample lpBuffer;
  YSE::reverb verb;
  Bool verbInit = false;
}

YSE::channelimpl& YSE::channelimpl::volume(Flt value) {
	_volume = value;
	Clamp(_volume, 0, 1);
	return (*this);
}

Flt YSE::channelimpl::volume() {
	return _volume;
}

YSE::channelimpl::channelimpl() {
	_volume = _lastVolume = 1;
	userChannel = true;
	_allowVirtual = true;
  _release = false;
	parent = NULL;
	reverb = NULL;
  link = NULL;

  _setVolume = false;
  _newVolume = 1;
  _currentVolume = 1;
  underWaterDepth = 0;

  if (UnderWater::verbInit == false) {
    UnderWater::verb.create();
    UnderWater::verb.preset(REVERB_UNDERWATER);
    UnderWater::verb.size(10);
    UnderWater::verb.active(false);
    UnderWater::verbInit = true;
  }
}

YSE::channelimpl::~channelimpl() {
  if (link) *link = NULL;
}

void YSE::channelimpl::update() {
  if (_setVolume) {
    volume(_newVolume);
    _setVolume = false;
  }
  _currentVolume = _volume;
}

Bool YSE::channelimpl::add(YSE::channelimpl * ch) {
	if (ch != this) {
		if (ch->parent != NULL) ch->parent->remove(ch);
		ch->parent = this;
		children.push_back(ch);
		return true;
	
	} else ch->parent = NULL;
	return false;
}

Bool YSE::channelimpl::remove(YSE::channelimpl *ch) {
	for (std::vector<channelimpl*>::iterator i = children.begin(); i != children.end(); i++) {
		if (*i == ch) {
			children.erase(i);
			return true;
		}
	}
	return false;
}

Bool YSE::channelimpl::add(YSE::soundimpl * s) {
	if (s->parent != NULL) {
		s->parent->remove(s);
	}
	s->parent = this;
	sounds.push_back(s);
	return true;
}

Bool YSE::channelimpl::remove(YSE::soundimpl *s) {
	for (std::vector<soundimpl*>::iterator i = sounds.begin(); i != sounds.end(); i++) {
		if (*i == s) {
			sounds.erase(i);
			return true;
		}
	}
	return false;
} 

YSE::channelimpl& YSE::channelimpl::set(Int count) {
	// delete current outputs if there are too much
	out.resize(count);
	outConf.resize(count);

	for (Int i = 0; i < children.size(); i++) {
		children[i]->set(count);
	}

	return (*this);
}

YSE::channelimpl& YSE::channelimpl::pos(Int nr, Flt angle) {
	if (nr >= 0 && nr < outConf.size()) outConf[nr].angle = angle; 
	for (Int i = 0; i < children.size(); i++) {
		children[i]->pos(nr, angle);
	}

	return (*this);
}

void YSE::channelimpl::clearBuffers() {
	for (Int i = 0; i < out.size(); i++) {
		out[i] = 0.0f;
	}
}

void YSE::channelimpl::dsp() {
	// if no sounds or other channels are linked, we skip this channel
	if (children.size() == 0 && sounds.size() == 0) return;

	// clear channel buffer
	for (Int i = 0; i < out.size(); i++) {
		out[i] = 0.0f;
	}

	// calculate child channels if there are any
	for (Int i = 0; i < children.size(); i++) {
		children[i]->dsp();
	}

	// calculate sounds in this channel
  for (std::vector<soundimpl*>::iterator i = sounds.begin(); i != sounds.end(); ++i) {
		if ((*i)->dsp()) {
      (*i)->toChannels();
    }
	}

  adjustVolume();


  if (underWaterDepth > 0) {
    UnderWater::verb.active(true);
    UnderWater::verb.pos(ListenerImpl._pos);
  } else {
    UnderWater::verb.active(false);
  }

	if (reverb != NULL) {
		reverb->process(out);
	}

  if (underWaterDepth > 1) {
    // sound underwater is more position neutral. Because the speed of sound 
    // is much higher, the ear cannot hear from what direction it comes.

    // first create a buffer that contains all sound with neutral positions
    for (int i = 0; i  < out.size(); i++) {
      UnderWater::buffer += out[i];
    }
    UnderWater::buffer /= (Flt)out.size();
    Flt factor = 140 - (underWaterDepth * 5);
    factor = DSP::MidiToFreq(factor);
    if (factor < 200) factor = 200;
    UnderWater::filter.setFrequency(factor);
    UnderWater::lpBuffer  = UnderWater::filter(UnderWater::buffer);

    if (underWaterDepth > 5.0f) {
      // completely disregard position info
      for (int i = 0; i  < out.size(); i++) {
        out[i] = UnderWater::lpBuffer;
      }
    } else {
      // partly replace with position-neutral version
      UnderWater::lpBuffer *= (underWaterDepth / 5.0f);
      for (int i = 0; i  < out.size(); i++) {
        out[i] *= (5 - underWaterDepth) / 5.0f;
        out[i] += UnderWater::lpBuffer;
      }
    }
  }

	// copy to parent channel unless this is the global channel
	if (parent != NULL) {
		buffersToParent();
	}

}

void YSE::channelimpl::adjustVolume() {
	if (_volume != _lastVolume) {
		// new value, create a ramp
		Flt length = 50.0f;
    Clamp(length, 1, out[0].getLength());
		Flt step = (_volume - _lastVolume) / BUFFERSIZE;
	
		for (Int i = 0; i < out.size(); i++) {
			Flt multiplier = _lastVolume;
      Flt * ptr = out[i].getBuffer();
			for (UInt i = 0; i < BUFFERSIZE; i++) {
				*ptr++ *= multiplier;
				multiplier += step;
			}
		}
    _lastVolume = _volume;
	} else {
		// same volume, just copy
		for (Int i = 0; i < out.size(); i++) {
			out[i] *= _volume;
		}
	}
}

void YSE::channelimpl::buffersToParent() {
	for (Int i = 0; i < out.size(); i++) {
		// parent size is not checked but should be ok because it's adjusted before calling this
		parent->out[i] += out[i];
	}
}

namespace YSE {
  void _setMono() {
	  YSE::ChannelGlobal.set(1)	.pos(0, 0);
  }

  void _setStereo() {
	  YSE::ChannelGlobal.set(2)	.pos(0, Pi/180.0f * -90.0f).pos(1, Pi/180.0f * 90.0f);
  }

  void _setQuad() {
	  YSE::ChannelGlobal.set(4)	.pos(0, Pi/180.0f * -45.0f).pos(1, Pi/180.0f *  45.0f)
														  .pos(2, Pi/180.0f * 135.0f).pos(3, Pi/180.0f * 135.0f);
  }

  void _set51() {
	  YSE::ChannelGlobal.set(5)	.pos(0, Pi/180.0f *  -45.0f).pos(1, Pi/180.0f *  45.0f)
														  .pos(2, Pi/180.0f *	   0.0f)
														  .pos(3, Pi/180.0f * -135.0f).pos(4, Pi/180.0f *	135.0f);
  }

  void _set51Side() {
	  YSE::ChannelGlobal.set(5)	.pos(0, Pi/180.0f * -45.0f).pos(1, Pi/180.0f * 45.0f)
														  .pos(2, Pi/180.0f *		0.0f)
														  .pos(3, Pi/180.0f * -90.0f).pos(4, Pi/180.0f * 90.0f);
  }

  void _set61() {
	  YSE::ChannelGlobal.set(5)	.pos(0, Pi/180.0f * -45.0f).pos(1, Pi/180.0f * 45.0f)
														  .pos(2, Pi/180.0f *	  0.0f)
														  .pos(3, Pi/180.0f * -90.0f).pos(4, Pi/180.0f * 90.0f)
														  .pos(5, Pi/180.0f * 180.0f);
  }

  void _set71() {
	  YSE::ChannelGlobal.set(5)	.pos(0, Pi/180.0f *  -45.0f).pos(1, Pi/180.0f *  45.0f)
														  .pos(2, Pi/180.0f *	   0.0f)
														  .pos(3, Pi/180.0f *  -90.0f).pos(4, Pi/180.0f *	 90.0f)
														  .pos(5, Pi/180.0f * -135.0f).pos(6, Pi/180.0f * 135.0f);
  }

  void _setAuto(Int count) {
	  switch(count) {
		  case	1: _setMono		(); break;
		  case	2: _setStereo	(); break;
		  case	4: _setQuad		(); break;
		  case	5: _set51			(); break;
		  case	6: _set61			(); break;
		  case	7: _set71			(); break;
		  default: _setStereo	(); break;
	  }
  }

  void ChangeChannelConf(CHANNEL_TYPE type, Int outputs) {
	  switch(type) {
		  case CT_AUTO	: _setAuto	(outputs); break;
		  case CT_MONO	: _setMono	(				); break;
		  case CT_STEREO: _setStereo(				); break;
		  case CT_QUAD	: _setQuad	(				); break;
		  case CT_51		: _set51		(				); break;
		  case CT_51SIDE: _set51Side(				); break;
		  case CT_61		:	_set61		(				); break;
		  case CT_71		:	_set71		(				); break;
		  case CT_CUSTOM: ChannelGlobal.set(outputs); break;
	  }
  }
}

YSE::channelimpl& YSE::channelimpl::allowVirtual(Bool value) {
	_allowVirtual = value;
	for (Int i = 0; i < children.size(); i++) {
		children[i]->allowVirtual(value);
	}
	return (*this);
}

Bool YSE::channelimpl::allowVirtual() {
	return _allowVirtual;
}

#include "stdafx.h"
#include "speakers.h"
#include "utils/misc.hpp"

YSE::speakerSet YSE::_Output;


YSE::speakerSet& YSE::speakerSet::set(Int count, bool sub) {
	this->sub = sub;
	// delete current speakers if there are any
	if (channel.size() != 0) channel.clear();

	for (Int i = 0; i < count; i++) {
		speaker s;
		s.angle = 0;
		channel.push_back(s);
	}
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::pos(Int nr, Flt angle) {
#pragma warning ( disable : 4018 )
	if (nr >= 0 && nr < channel.size()) channel[nr].angle = angle; // * TO_RADIANS; 
	return (*this);
}



/*void YSE::speakerSet::connect(void *channels, unsigned long length) {
	// on audio callback time, this is used to connect the audio buffer to the channels we use
#pragma warning ( disable : 4018 )
	for (Int i = 0; i < channel.size(); i++) {
		channel[i].buffer = (((Flt **) channels)[i], length);
		channel[i].buffer = 0.0f;
	}
}*/

YSE::speakerSet& YSE::speakerSet::setMono() {
	set(1).pos(0, 0);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::setStereo() {
	set(2).pos(0, Pi/180.0f * -90.0f).pos(1, Pi/180.0f * 90.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::setQuad() {
	set(4).pos(0, Pi/180.0f * -135.0f).pos(1, Pi/180.0f * -45.0f)
				.pos(2, Pi/180.0f *		45.0f).pos(3, Pi/180.0f * 135.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::set51() {
	set(5, true).pos(0, Pi/180.0f * -110.0f).pos(1, Pi/180.0f * -30.0f)
							.pos(2, Pi/180.0f *		 0.0f).pos(3, Pi/180.0f *  30.0f)
							.pos(4, Pi/180.0f *	 110.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::set51Side() {
	set(5, true).pos(0, Pi/180.0f * -90.0f).pos(1, Pi/180.0f * -30.0f)
							.pos(2, Pi/180.0f *		0.0f).pos(3, Pi/180.0f *  30.0f)
							.pos(4, Pi/180.0f *	 90.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::set61() {
	set(5, true).pos(0, Pi/180.0f * -90.0f).pos(1, Pi/180.0f * -30.0f)
							.pos(2, Pi/180.0f *	  0.0f).pos(3, Pi/180.0f *  30.0f)
							.pos(4, Pi/180.0f *	 90.0f).pos(5, Pi/180.0f * 180.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::set71() {
	set(5, true).pos(0, Pi/180.0f * -150.0f).pos(1, Pi/180.0f * -90.0f)
							.pos(2, Pi/180.0f *	 -30.0f).pos(3, Pi/180.0f *   0.0f)
							.pos(4, Pi/180.0f *	  30.0f).pos(5, Pi/180.0f *  90.0f)
							.pos(6, Pi/180.0f *  150.0f);
	return (*this);
}

YSE::speakerSet& YSE::speakerSet::setAuto(Int count) {
	switch(count) {
		case	1: setMono	(); break;
		case	2: setStereo(); break;
		case	4: setQuad	(); break;
		case	5: set51		(); break;
		case	6: set61		(); break;
		case	7: set71		(); break;
		default: setStereo(); break;
	}
	return (*this);
}
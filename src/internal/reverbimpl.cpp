#include "stdafx.h"
#include "reverbimpl.h"
#include "utils/misc.hpp"

YSE::reverbimpl::reverbimpl() {
	_active = true;
	_roomsize = 0.5;
	_damp = 0.5;
	_wet = 0.5;
	_dry = 0.5;
	_modFrequency = 0;
	_modWidth = 0;
	for (Int i = 0; i < 4; i++) {
		_earlyPtr[i] = 0;
		_earlyGain[i] = 0;
	}
	global = false;
  _release = false;
  link = NULL;
}

YSE::reverbimpl::~reverbimpl() {
  if (link) *link = NULL;
}

YSE::reverbimpl::reverbimpl(const YSE::reverbimpl & cp) {
	_position = cp._position;
	_size = cp._size;
	_rolloff = cp._rolloff;
	_active = cp._active;
	_roomsize = cp._roomsize;
	_damp = cp._damp;
	_wet = cp._wet;
	_dry = cp._dry;
	_modFrequency = cp._modFrequency;
	_modWidth = cp._modWidth;
	for (Int i = 0; i < 4; i++) {
		_earlyPtr[i] = cp._earlyPtr[i];
		_earlyGain[i] = cp._earlyGain[i];
	}
	global = false;
  _release = false;
  link = NULL;
}

YSE::reverbimpl& YSE::reverbimpl::operator+=(const reverbimpl& p) {
	_roomsize += p._roomsize;
	_damp += p._damp;
	_wet += p._wet;
	_dry += p._dry;
	_modFrequency += p._modFrequency;
	_modWidth += p._modWidth;
	for (Int i = 0; i < 4; i++) {
		_earlyPtr[i] += p._earlyPtr[i];
		_earlyGain[i] += p._earlyGain[i];
	}
  return (*this);
}

YSE::reverbimpl& YSE::reverbimpl::operator*=(Flt value) {
	_roomsize *= value;
	_damp *= value;
	_wet *= value;
	_dry *= value;
	_modFrequency *= value;
	_modWidth *= value;
	for (Int i = 0; i < 4; i++) {
		_earlyPtr[i] *= value;
		_earlyGain[i] *= value;
	}
  return (*this);
}

YSE::reverbimpl YSE::reverbimpl::operator*(Flt value) {
  reverbimpl result(*this);
	result._roomsize *= value;
	result._damp *= value;
	result._wet *= value;
	result._dry *= value;
	result._modFrequency *= value;
	result._modWidth *= value;
	for (Int i = 0; i < 4; i++) {
		result._earlyPtr[i] *= value;
		result._earlyGain[i] *= value;
	}
  return result;
}

YSE::reverbimpl& YSE::reverbimpl::operator/=(Flt value) {
	_roomsize /= value;
	_damp /= value;
	_wet /= value;
	_dry /= value;
	_modFrequency /= value;
	_modWidth /= value;
	for (Int i = 0; i < 4; i++) {
		_earlyPtr[i] /= value;
		_earlyGain[i] /= value;
	}
  return (*this);
}

YSE::reverbimpl& YSE::reverbimpl::pos(const Vec & value) {
	_position = value;
	return (*this);
}

YSE::Vec YSE::reverbimpl::pos() {
	return _position;
}

YSE::reverbimpl& YSE::reverbimpl::size(Flt value) {
	if (value < 0) value = 0;
	_size = value;
	return (*this);
}

Flt YSE::reverbimpl::size() {
	return _size;
}

YSE::reverbimpl& YSE::reverbimpl::rolloff(Flt value) {
	if (value < 0) value = 0;
	_rolloff = value;
	return (*this);
}

Flt YSE::reverbimpl::rolloff() {
	return _rolloff;
}

YSE::reverbimpl& YSE::reverbimpl::active(Bool value) {
	_active = value;
	return (*this);
}

Bool YSE::reverbimpl::active() {
	return _active;
}

YSE::reverbimpl& YSE::reverbimpl::roomsize(Flt value) {
	Clamp(value, 0, 1);
	_roomsize = value;
	return (*this);
}

Flt YSE::reverbimpl::roomsize() {
	return _roomsize;
}

YSE::reverbimpl& YSE::reverbimpl::damp(Flt value) {
	Clamp(value, 0, 1);
	_damp = value;
	return (*this);
}

Flt YSE::reverbimpl::damp() {
	return _damp;
}

YSE::reverbimpl& YSE::reverbimpl::wet(Flt value) {
	Clamp(value, 0, 1);
	_wet = value;
	return (*this);
}

Flt YSE::reverbimpl::wet() {
	return _wet;
}

YSE::reverbimpl& YSE::reverbimpl::dry(Flt value) {
	Clamp(value, 0 ,1);
	_dry = value;
	return (*this);
}

Flt YSE::reverbimpl::dry() {
	return _dry;
}

YSE::reverbimpl& YSE::reverbimpl::modFreq(Flt value) {
	if (value < 0) value = 0;
	_modFrequency = value;
	return (*this);
}

Flt YSE::reverbimpl::modFreq() {
	return _modFrequency;
}

YSE::reverbimpl& YSE::reverbimpl::modWidth(Flt value) {
	if (value < 0) value = 0;
	_modWidth = value;
	return (*this);
}

Flt YSE::reverbimpl::modWidth() {
	return _modWidth;
}

YSE::reverbimpl& YSE::reverbimpl::reflectionTime(Int reflection, Int value) {
	if (reflection > -1 && reflection < 4) {
		Clamp(value, 0, 2999);
		_earlyPtr[reflection] = value;
	}
	return (*this);
}

Int YSE::reverbimpl::reflectionTime(Int reflection) {
	if (reflection > -1 && reflection < 4) return _earlyPtr[reflection];
	return -1;
}

YSE::reverbimpl& YSE::reverbimpl::reflectionGain(Int reflection, Flt value) {
	if (reflection > -1 && reflection < 4) {
		Clamp(value, 0, 1);
		_earlyGain[reflection] = value;
	}
	return (*this);
}

Flt YSE::reverbimpl::reflectionGain(Int reflection) {
	if (reflection > -1 && reflection < 4) return _earlyGain[reflection];
	return -1;
}

YSE::reverbimpl& YSE::reverbimpl::preset(REVERB_PRESET value) {
	switch (value) {
	case REVERB_OFF:			roomsize(0).damp(0).wet(0).dry(1).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
	case REVERB_GENERIC:	roomsize(0.5).damp(0.5).wet(0.4).dry(0.6).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
	case REVERB_PADDED:		roomsize(0.1).damp(0.9).wet(0.1).dry(0.9).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
	case REVERB_ROOM:			roomsize(0.3).damp(0.8).wet(0.3).dry(0.7).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
	case REVERB_BATHROOM:	roomsize(0.2).damp(0.1).wet(0.7).dry(0.3).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 20).reflectionTime(2,50).reflectionTime(3,85);
												reflectionGain(0, 1).reflectionGain(1,0.7).reflectionGain(2,0.5).reflectionGain(3,0.3);
												break;
	case REVERB_STONEROOM:roomsize(0.3).damp(0.01).wet(0.7).dry(0.3).modFreq(0).modWidth(0);
												reflectionTime(0, 30).reflectionTime(1, 70).reflectionTime(2,100).reflectionTime(3,150);
												reflectionGain(0,0.8).reflectionGain(1,0.3).reflectionGain(2,0.5).reflectionGain(3,0.3);
												break;
	case REVERB_LARGEROOM:roomsize(0.7).damp(0.8).wet(0.3).dry(0.7).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
  case REVERB_HALL:     roomsize(0.7).damp(0.4).wet(0.5).dry(0.5).modFreq(0).modWidth(0);
												reflectionTime(0, 0).reflectionTime(1, 0).reflectionTime(2,0).reflectionTime(3,0);
												reflectionGain(0,0).reflectionGain(1,0).reflectionGain(2,0).reflectionGain(3,0);
												break;
  case REVERB_CAVE:     roomsize(1.0).damp(0.3).wet(0.7).dry(0.3).modFreq(0).modWidth(0);
												reflectionTime(0, 100).reflectionTime(1, 250).reflectionTime(2,400).reflectionTime(3,800);
												reflectionGain(0,0.8).reflectionGain(1,0.6).reflectionGain(2,0.4).reflectionGain(3,0.5);
												break;
  case REVERB_SEWERPIPE:roomsize(0.5).damp(0.1).wet(0.7).dry(0.3).modFreq(3.5f).modWidth(20.0f);
												reflectionTime(0, 200).reflectionTime(1, 600).reflectionTime(2,1100).reflectionTime(3,0);
												reflectionGain(0,0.05).reflectionGain(1,0.04).reflectionGain(2,0.01).reflectionGain(3,0);
												break;
  case REVERB_UNDERWATER: roomsize(0.1).damp(0.2).wet(0.7).dry(0.3).modFreq(3.5f).modWidth(20.0f);
												  reflectionTime(0, 0).reflectionTime(1,0).reflectionTime(2,0).reflectionTime(3,0);
												  reflectionGain(0,0).reflectionGain(1,00).reflectionGain(2,0).reflectionGain(3,0);
												  break;
  }


  return (*this);
}
	
/*																			// roomlevel					// decaytime					early   delay			late delay	 mod time depth  //        //     diff     dens
  #define FMOD_PRESET_GENERIC					{ -1000,  -100,   0,   1.49f,  0.83f, 1.0f,  -2602, 0.007f,   200, 0.011f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_PADDEDCELL			{ -1000,  -6000,  0,   0.17f,  0.10f, 1.0f,  -1204, 0.001f,   207, 0.002f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_ROOM						{ -1000,  -454,   0,   0.40f,  0.83f, 1.0f,  -1646, 0.002f,    53, 0.003f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_BATHROOM				{ -1000,  -1200,  0,   1.49f,  0.54f, 1.0f,   -370, 0.007f,  1030, 0.011f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f,  60.0f, 0x3f }
  #define FMOD_PRESET_LIVINGROOM			{ -1000,  -6000,  0,   0.50f,  0.10f, 1.0f,  -1376, 0.003f, -1104, 0.004f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_STONEROOM				{ -1000,  -300,   0,   2.31f,  0.64f, 1.0f,   -711, 0.012f,    83, 0.017f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_AUDITORIUM			{ -1000,  -476,   0,   4.32f,  0.59f, 1.0f,   -789, 0.020f,  -289, 0.030f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_CONCERTHALL			{ -1000,  -500,   0,   3.92f,  0.70f, 1.0f,  -1230, 0.020f,    -2, 0.029f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_CAVE						{ -1000,  0,      0,   2.91f,  1.30f, 1.0f,   -602, 0.015f,  -302, 0.022f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x1f }
  #define FMOD_PRESET_ARENA						{ -1000,  -698,   0,   7.24f,  0.33f, 1.0f,  -1166, 0.020f,    16, 0.030f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_HANGAR					{ -1000,  -1000,  0,   10.05f, 0.23f, 1.0f,   -602, 0.020f,   198, 0.030f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_CARPETTEDHALLWAY{ -1000,  -4000,  0,   0.30f,  0.10f, 1.0f,  -1831, 0.002f, -1630, 0.030f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_HALLWAY					{ -1000,  -300,   0,   1.49f,  0.59f, 1.0f,  -1219, 0.007f,   441, 0.011f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_STONECORRIDOR		{ -1000,  -237,   0,   2.70f,  0.79f, 1.0f,  -1214, 0.013f,   395, 0.020f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_ALLEY						{ -1000,  -270,   0,   1.49f,  0.86f, 1.0f,  -1204, 0.007f,    -4, 0.011f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_FOREST					{ -1000,  -3300,  0,   1.49f,  0.54f, 1.0f,  -2560, 0.162f,  -229, 0.088f, 0.25f, 0.000f, 5000.0f, 250.0f,  79.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_CITY						{ -1000,  -800,   0,   1.49f,  0.67f, 1.0f,  -2273, 0.007f, -1691, 0.011f, 0.25f, 0.000f, 5000.0f, 250.0f,  50.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_MOUNTAINS				{ -1000,  -2500,  0,   1.49f,  0.21f, 1.0f,  -2780, 0.300f, -1434, 0.100f, 0.25f, 0.000f, 5000.0f, 250.0f,  27.0f, 100.0f, 0x1f }
  #define FMOD_PRESET_QUARRY					{ -1000,  -1000,  0,   1.49f,  0.83f, 1.0f, -10000, 0.061f,   500, 0.025f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_PLAIN						{ -1000,  -2000,  0,   1.49f,  0.50f, 1.0f,  -2466, 0.179f, -1926, 0.100f, 0.25f, 0.000f, 5000.0f, 250.0f,  21.0f, 100.0f, 0x3f }
  #define FMOD_PRESET_PARKINGLOT			{ -1000,  0,      0,   1.65f,  1.50f, 1.0f,  -1363, 0.008f, -1153, 0.012f, 0.25f, 0.000f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x1f }
  #define FMOD_PRESET_SEWERPIPE				{ -1000,  -1000,  0,   2.81f,  0.14f, 1.0f,    429, 0.014f,  1023, 0.021f, 0.25f, 0.000f, 5000.0f, 250.0f,  80.0f,  60.0f, 0x3f }
  #define FMOD_PRESET_UNDERWATER			{ -1000,  -4000,  0,   1.49f,  0.10f, 1.0f,   -449, 0.007f,  1700, 0.011f, 1.18f, 0.348f, 5000.0f, 250.0f, 100.0f, 100.0f, 0x3f }
 */
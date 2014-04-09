/*
  ==============================================================================

    note.cpp
    Created: 4 Apr 2014 10:22:39am
    Author:  yvan vander sanden

  ==============================================================================
*/

#include "note.hpp"
#include "../internalHeaders.h"

YSE::MUSIC::note::note(Flt pitch, Flt volume) : pitch(pitch), volume(volume) {}

YSE::MUSIC::note::note(const note & object) {
    pitch = object.pitch;
    volume = object.volume;
}

YSE::MUSIC::note & YSE::MUSIC::note::set(Flt pitch, Flt volume) {
    this->pitch = pitch;
    this->volume = volume;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::setPitch(Flt value) {
    this->pitch = value;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::setVolume(Flt value) {
    this->volume = value;
    return *this;
}

Flt YSE::MUSIC::note::getPitch() {
    return pitch;
}

Flt YSE::MUSIC::note::getVolume() {
    return volume;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator+=(const note & object) {
    this->pitch += object.pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator-=(const note &object) {
    this->pitch -= object.pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator*=(const note & object) {
    this->pitch *= object.pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator/=(const note &object) {
    this->pitch /= object.pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator+=(Flt pitch) {
    this->pitch += pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator-=(Flt pitch) {
    this->pitch -= pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator/=(Flt pitch) {
    this->pitch /= pitch;
    return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::operator*=(Flt pitch) {
    this->pitch *= pitch;
    return *this;
}

Bool YSE::MUSIC::note::operator==(const YSE::MUSIC::note &object) {
    return pitch == object.pitch;
}

Bool YSE::MUSIC::note::operator!=(const YSE::MUSIC::note &object) {
    return pitch != object.pitch;
}

Bool YSE::MUSIC::note::operator<(const YSE::MUSIC::note &object) {
    return pitch < object.pitch;
}

Bool YSE::MUSIC::note::operator>(const YSE::MUSIC::note &object) {
    return pitch > object.pitch;
}

Bool YSE::MUSIC::note::operator<=(const YSE::MUSIC::note &object) {
    return pitch <= object.pitch;
}

Bool YSE::MUSIC::note::operator>=(const YSE::MUSIC::note &object) {
    return pitch >= object.pitch;
}

Bool YSE::MUSIC::note::operator==(Flt pitch) {
    return this->pitch == pitch;
}

Bool YSE::MUSIC::note::operator!=(Flt pitch) {
    return this->pitch != pitch;
}

Bool YSE::MUSIC::note::operator<(Flt pitch) {
    return this->pitch < pitch;
}

Bool YSE::MUSIC::note::operator>(Flt pitch) {
    return this->pitch > pitch;
}

Bool YSE::MUSIC::note::operator<=(Flt pitch) {
    return this->pitch <= pitch;
}

Bool YSE::MUSIC::note::operator>=(Flt pitch) {
    return this->pitch >= pitch;
}

YSE::MUSIC::note YSE::MUSIC::operator+(const note &n, Flt pitch) {
    note out = n;
    out += pitch;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator-(const note &n, Flt pitch) {
    note out = n;
    out -= pitch;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator*(const note &n, Flt pitch) {
    note out = n;
    out *= pitch;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator/(const note &n, Flt pitch) {
    note out = n;
    out /= pitch;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator+(Flt pitch, const note &n) {
    note out(pitch);
    out += n.pitch;
    out.setVolume(n.volume);
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator-(Flt pitch, const note &n) {
    note out(pitch);
    out -= n.pitch;
    out.setVolume(n.volume);
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator*(Flt pitch, const note &n) {
    note out(pitch);
    out *= n.pitch;
    out.setVolume(n.volume);
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator/(Flt pitch, const note &n) {
    note out(pitch);
    out /= n.pitch;
    out.setVolume(n.volume);
    return out;
}


YSE::MUSIC::note YSE::MUSIC::operator+(const note &n1, const note &n2) {
    note out(n1);
    out += n2;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator-(const note &n1, const note &n2) {
    note out(n1);
    out -= n2;
    return out;
}

YSE::MUSIC::note YSE::MUSIC::operator*(const note &n1, const note &n2) {
    note out(n1);
    out *= n2;
    return out;
}

YSE ::MUSIC::note YSE::MUSIC::operator/(const note&n1, const note &n2) {
    note out(n1);
    out /= n2;
    return out;
}


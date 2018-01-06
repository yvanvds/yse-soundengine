/*
  ==============================================================================

    note.cpp
    Created: 4 Apr 2014 10:22:39am
    Author:  yvan vander sanden

  ==============================================================================
*/

#include "note.hpp"
#include "../internalHeaders.h"
#include "pNote.hpp"
#include <iostream>

YSE::MUSIC::note::note(Flt pitch, Flt volume, Flt length, Int channel) : pitch(pitch), volume(volume), length(length), channel(channel) {}

YSE::MUSIC::note::note(const note & object) {
    pitch = object.pitch;
    volume = object.volume;
    length = object.length;
    channel = object.channel;
}

YSE::MUSIC::note & YSE::MUSIC::note::set(Flt pitch, Flt volume, Flt length, Int channel) {
    this->pitch = pitch;
    this->volume = volume;
    this->length = length;
    this->channel = channel;
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

YSE::MUSIC::note & YSE::MUSIC::note::setLength(Flt value) {
  this->length = value;
  return *this;
}

YSE::MUSIC::note & YSE::MUSIC::note::setChannel(Int value) {
  this->channel = value;
  return *this;
}

Flt YSE::MUSIC::note::getPitch() const {
    return pitch;
}

Flt YSE::MUSIC::note::getVolume() const {
    return volume;
}

Flt YSE::MUSIC::note::getLength() const {
  return length;
}

Int YSE::MUSIC::note::getChannel() const {
  return channel;
}

bool YSE::MUSIC::note::update() {
  length -= YSE::INTERNAL::Time().delta();
  return length > 0;
}

bool YSE::MUSIC::note::update(Flt delta) {
  length -= delta;
  return length > 0;
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

void YSE::MUSIC::note::operator=(const pNote& other) {
  this->channel = other.channel;
  this->length = other.length;
  this->pitch = other.pitch;
  this->volume = other.volume;
}
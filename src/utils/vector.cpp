#include "stdafx.h"
#include "utils/vector.hpp"
#include <cmath>
#include <iostream>
#include <sstream>

Flt YSE::Dist(const Vec &a, const Vec &b) {
	return sqrt(std::pow((b.x - a.x), 2) + std::pow((b.y - a.y), 2) + std::pow((b.z - a.z), 2));
}

Flt YSE::Vec::length() {
	return sqrt(std::pow(x,2) + std::pow(y,2) + std::pow(z,2));
}

Bool YSE::Vec::operator==(const Vec &v) const {
	if (x == v.x && y == v.y && z == v.z) return true;
	return false;
}

Bool YSE::Vec::operator!=(const Vec &v) const {
	if (x != v.x || y != v.y || z != v.z) return true;
	return false;
}

std::string YSE::Vec::asText() {
	std::stringstream result(std::stringstream::in | std::stringstream::out);
	result << "X: " << x << " Y: " << y << " Z: " << z;
	return result.str();
}
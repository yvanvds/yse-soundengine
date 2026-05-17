#include "yse_c/yse_listener.h"
#include "yse_c_internal.hpp"

#include "../listener.hpp"
#include "../utils/vector.hpp"

namespace {
  inline YSE::listener* to_cpp(YseListener* l) {
    return reinterpret_cast<YSE::listener*>(l);
  }

  inline yse_pos_t to_c(const YSE::Pos& p) {
    yse_pos_t out;
    out.x = p.x;
    out.y = p.y;
    out.z = p.z;
    return out;
  }

  inline YSE::Pos to_cpp(const yse_pos_t& p) {
    return YSE::Pos(p.x, p.y, p.z);
  }
}

extern "C" {

YSE_C_API YseListener* yse_listener_get(void) {
  return reinterpret_cast<YseListener*>(&YSE::Listener());
}

YSE_C_API void yse_listener_set_pos(YseListener* l, const yse_pos_t* p) {
  if (!l || !p) return;
  to_cpp(l)->pos(to_cpp(*p));
}

YSE_C_API yse_pos_t yse_listener_get_pos(YseListener* l) {
  if (!l) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(l)->pos());
}

YSE_C_API yse_pos_t yse_listener_get_vel(YseListener* l) {
  if (!l) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(l)->vel());
}

YSE_C_API yse_pos_t yse_listener_get_forward(YseListener* l) {
  if (!l) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(l)->forward());
}

YSE_C_API yse_pos_t yse_listener_get_upward(YseListener* l) {
  if (!l) return yse_pos_t{0, 0, 0};
  return to_c(to_cpp(l)->upward());
}

YSE_C_API void yse_listener_set_orient(
    YseListener* l, const yse_pos_t* forward, const yse_pos_t* up) {
  if (!l || !forward || !up) return;
  to_cpp(l)->orient(to_cpp(*forward), to_cpp(*up));
}

} // extern "C"

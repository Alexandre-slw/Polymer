#pragma once
#include "types.h"

namespace polymer {

// TODO: Make this more advanced
struct InputState {
  bool forward;
  bool backward;
  bool left;
  bool right;
  bool climb;
  bool fall;
  bool sprint;
  bool display_players;

  u64 jump_time;
};

} // namespace polymer

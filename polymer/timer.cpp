#include "timer.h"

Timer::Timer(int ticks) : interval_ms(1000.0 / ticks) {}

void Timer::Update(float delta_frame) {
  this->delta_frame = delta_frame;

  ticks_to_process = 0;
  accumulated_ms += delta_frame;

  while (accumulated_ms >= interval_ms) {
    accumulated_ms -= interval_ms;
    ticks_to_process++;
  }

  delta_tick = accumulated_ms / interval_ms;
}

double Timer::GetIntervalSeconds() const {
  return interval_ms / 1000.0;
}

double Timer::GetDeltaTick() const {
  return delta_tick;
}

float Timer::GetDeltaFrame() const {
  return delta_frame;
}

int Timer::GetTicksToProcess() const {
  return ticks_to_process;
}

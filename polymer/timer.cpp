#include "timer.h"

Timer::Timer(int ticks) : interval_ms(1000.0 / ticks) {}

void Timer::Update(float delta_frame) {
  ticks_to_process = 0;
  accumulated_ms += delta_frame;

  while (accumulated_ms >= interval_ms) {
    accumulated_ms -= interval_ms;
    ticks_to_process++;
  }
}

double Timer::GetIntervalSeconds() const {
  return interval_ms / 1000.0;
}

double Timer::GetDeltaTick() const {
  return accumulated_ms / interval_ms;
}

int Timer::GetTicksToProcess() const {
  return ticks_to_process;
}

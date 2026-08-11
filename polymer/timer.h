#pragma once

class Timer {

private:
  double interval_ms = 0;
  double accumulated_ms = 0;
  int ticks_to_process = 0;

  double delta_tick;
  float delta_frame;


public:
  Timer(int ticks);

  void Update(float delta_frame);

  double GetIntervalSeconds() const;
  double GetDeltaTick() const;
  float GetDeltaFrame() const;
  int GetTicksToProcess() const;

};
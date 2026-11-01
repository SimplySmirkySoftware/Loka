#ifndef LOKA_TOOLBOX_IDLE_CLOCK_HPP
#define LOKA_TOOLBOX_IDLE_CLOCK_HPP

/** The run-local idle clock; a wrapped native tick contributes no elapsed time. */
class ToolboxIdleClock
{
public:
  explicit ToolboxIdleClock(unsigned long now)
      : lastTick_(now)
  {
  }
  double elapsedSeconds(unsigned long now)
  {
    double elapsed = 0.0;
    if (now >= this->lastTick_)
      elapsed = static_cast<double>(now - this->lastTick_) / 60.0;
    this->lastTick_ = now;
    return elapsed;
  }

private:
  unsigned long lastTick_;
};
#endif

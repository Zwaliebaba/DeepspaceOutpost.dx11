#pragma once

class Event
{
  public:
  bool operator==(const Event&) const = default;

  Event()
    : m_consumed(false)
  {}

  void SetConsumed() { m_consumed = true; }
  bool IsConsumed() const { return m_consumed; }

  protected:
  bool m_consumed;
  virtual ~Event() = default;
};

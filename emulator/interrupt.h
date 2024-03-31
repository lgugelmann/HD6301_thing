#ifndef EIGHT_BIT_COMPUTER_INTERRUPT_H
#define EIGHT_BIT_COMPUTER_INTERRUPT_H

#include <set>

namespace eight_bit {

class Interrupt {
 public:
  Interrupt() = default;
  ~Interrupt() = default;

  // Set an interrupt. The returned integer needs to be used to clear the
  // interrupt again.
  int set_interrupt() {
    int interrupt_id = next_interrupt_id_;
    // Technically speaking this could wrap around while something is still
    // active. We're going to ignore it and just say that maybe a few billion
    // interrupts are too many.
    next_interrupt_id_ += 1;
    interrupts_.insert(interrupt_id);
    return interrupt_id;
  }

  // Clear the interrupt `interrupt`.
  void clear_interrupt(int interrupt) { interrupts_.erase(interrupt); }

  // Returns true if this interrupt is firing (i.e. has outstanding interrupts),
  // false otherwise.
  bool has_interrupt() { return interrupts_.size() > 0; }

 private:
  int next_interrupt_id_ = 0;
  std::set<int> interrupts_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_COMPUTER_INTERRUPT_H
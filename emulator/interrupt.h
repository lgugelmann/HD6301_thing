#ifndef EIGHT_BIT_COMPUTER_INTERRUPT_H
#define EIGHT_BIT_COMPUTER_INTERRUPT_H

#include <set>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"

namespace eight_bit {

// A simple interrupt class that can be used to set and clear interrupts. Thread
// safe.
class Interrupt {
 public:
  Interrupt() = default;
  ~Interrupt() = default;

  // Set an interrupt. The returned integer needs to be used to clear the
  // interrupt again.
  int set_interrupt() {
    absl::MutexLock lock(&mutex_);
    int interrupt_id = next_interrupt_id_;
    // Technically speaking this could wrap around while something is still
    // active. We're going to ignore it and just say that maybe a few billion
    // interrupts are too many.
    next_interrupt_id_ += 1;
    if (next_interrupt_id_ == 0) {
      next_interrupt_id_ = 1;
    }
    interrupts_.insert(interrupt_id);
    has_interrupt_.test_and_set();
    return interrupt_id;
  }

  // Clear the interrupt `interrupt`.
  void clear_interrupt(int interrupt) {
    absl::MutexLock lock(&mutex_);
    interrupts_.erase(interrupt);
    if (interrupts_.empty()) {
      has_interrupt_.clear();
    }
  }

  // Returns true if this interrupt is firing (i.e. has outstanding interrupts),
  // false otherwise.
  bool has_interrupt() { return has_interrupt_.test(); }

 private:
  // The next interrupt ID to use. Value 0 is never a valid interrupt ID and can
  // be used as a sentinel for 'no interrupt set'.
  absl::Mutex mutex_;
  // Locking the mutex on each emulator cycle is expensive enough to show up on
  // profiles. An atomic flag is much cheaper for this.
  std::atomic_flag has_interrupt_ = ATOMIC_FLAG_INIT;
  int next_interrupt_id_ ABSL_GUARDED_BY(mutex_) = 1;
  std::set<int> interrupts_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_COMPUTER_INTERRUPT_H
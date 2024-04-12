#ifndef EIGHT_BIT_COMPUTER_INTERRUPT_H
#define EIGHT_BIT_COMPUTER_INTERRUPT_H

#include <absl/base/thread_annotations.h>
#include <absl/synchronization/mutex.h>

#include <set>

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
    return interrupt_id;
  }

  // Clear the interrupt `interrupt`.
  void clear_interrupt(int interrupt) {
    absl::MutexLock lock(&mutex_);
    interrupts_.erase(interrupt);
  }

  // Returns true if this interrupt is firing (i.e. has outstanding interrupts),
  // false otherwise.
  bool has_interrupt() {
    absl::MutexLock lock(&mutex_);
    return interrupts_.size() > 0;
  }

 private:
  // The next interrupt ID to use. Value 0 is never a valid interrupt ID and can
  // be used as a sentinel for 'no interrupt set'.
  absl::Mutex mutex_;
  int next_interrupt_id_ ABSL_GUARDED_BY(mutex_) = 1;
  std::set<int> interrupts_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_COMPUTER_INTERRUPT_H
#ifndef EIGHT_BIT_PORT_H
#define EIGHT_BIT_PORT_H

#include <cstdint>
#include <functional>
#include <string>

namespace eight_bit {

class IOPort {
 public:
  typedef std::function<uint8_t()> read_callback;
  typedef std::function<void(uint8_t)> write_callback;

  IOPort(std::string_view name);
  ~IOPort() = default;

  // Reads from the port. Note: assumes that the read callbacks do not return
  // overlapping data and that they leave bits that aren't part of the callback
  // device as 0.
  uint8_t read();

  // Writes to this port. Calls all write callbacks with the written data. Bits
  // set as inputs are passed to the callbacks as zero.
  void write(uint8_t data);

  // Set the data direction for the port. Bits set to 0 are inputs, bits set to
  // 1 are outputs.
  void set_direction(uint8_t direction_mask);

  // Get the data direction for the port.
  uint8_t get_direction() const;

  // Registers a callback that's called when read() is invoked on this port. The
  // device at the other end is expected to provide data.
  void register_read_callback(const read_callback& callback);

  // Registers a write callback for this port.
  void register_write_callback(const write_callback& callback);

 private:
  std::string name_;
  // Callbacks for reading and writing to the port.
  std::vector<read_callback> read_callbacks_;
  std::vector<write_callback> write_callbacks_;

  // Bits set to 0 are inputs, bits set to 1 are outputs.
  uint8_t data_direction_ = 0;

  // The data last written to the port. This is returned for output bits on
  // reads.
  uint8_t data_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_PORT_H
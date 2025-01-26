#ifndef EIGHT_BIT_PORT_H
#define EIGHT_BIT_PORT_H

#include <cstdint>
#include <functional>
#include <string>

namespace eight_bit {

// IOPort models a set of 8 digital pins that can be used as inputs or outputs
// from an emulated chip. What makes an "input" or an "output" is really context
// dependent - but generally it's easiest to think of it from the perspective of
// the chip the I/O port belongs to. The model logically is like having an
// input, an output, and a data direction register. The data direction register
// selects which pins are inputs and which are outputs. The output pins reflect
// the data set in the output register. A read of the input register reflects
// the current state of inputs for pins configured as input. For pins configured
// as output it returns the value in the output register. There is a
// callback-driven API to either register for output pin changes, or to provide
// data on-the-fly for input pins. There are also APIs to drive outputs (from
// "inside" the chip) or inputs (from "outside" the chip).
class IOPort {
 public:
  typedef std::function<uint8_t()> input_read_callback;
  typedef std::function<void(uint8_t)> output_change_callback;
  typedef std::function<void(uint8_t)> input_change_callback;

  IOPort(std::string_view name);
  ~IOPort() = default;

  // Reads from the port, i.e. return the data that was provided as input to it
  // via provide_inputs(). For bits set as outputs in the data direction
  // register we return the corresponding bit in the output register.
  uint8_t read_input_register();

  // Returns the contents of the output register.
  uint8_t read_output_register();

  // Writes to this port, i.e. set the output register to 'data'. Calls all
  // registered output-change callbacks with the new data. Bits set as inputs in
  // the data direction register are passed to the callbacks as zero, regardless
  // of the output register state.
  void write_output_register(uint8_t data);

  // Provides data to input pins. Only bits that are 1 in 'mask' are updated.
  void provide_inputs(uint8_t data, uint8_t mask = 0xff);

  // Set the data direction for the port. Bits set to 0 denote, bits set to 1
  // denote outputs.
  void write_data_direction_register(uint8_t direction_mask);

  // Return the data direction for the port.
  uint8_t read_data_direction_register() const;

  // Registers a callback that's called when outputs change for this port.
  void register_output_change_callback(const output_change_callback& callback);

  // Registers a callback that's called when input pins change via
  // provide_inputs().
  void register_input_change_callback(const input_change_callback& callback);

 private:
  std::string name_;
  // Callbacks for event-driven reaction to changes.
  std::vector<output_change_callback> output_change_callbacks_;
  std::vector<input_change_callback> input_change_callbacks_;

  // Bits set to 0 are inputs, bits set to 1 are outputs.
  uint8_t data_direction_ = 0;
  uint8_t output_register_ = 0;
  uint8_t input_register_ = 0;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_PORT_H
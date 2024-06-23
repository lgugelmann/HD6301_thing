# Graphics state library

This is a small library that allows sharing most of the graphics code (command
parsing, etc) between the emulator and the pico_graphics firmware.

It's heavily geared towards being easy to use in the Pico code. That's why the
color formats are weird and there are a few `inline` hints.

If the library is included in a project which also has the Pico SDK loaded and
the `PICO_COPY_TO_RAM` option set, then all functions are decorated with the
`__not_in_flash()` hint to make sure they run from RAM. Without these hints the
code is too slow.

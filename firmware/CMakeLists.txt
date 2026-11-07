cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tr66_sp1 LANGUAGES C)

target_include_directories(app PRIVATE ../src)
target_sources(app PRIVATE
    src/main.c
    src/codecs.c
    src/usb_midi.c
    ../src/controls.c
    ../src/midi.c
    ../src/patterns.c
    ../src/sequencer.c
    ../src/tr66.c
)

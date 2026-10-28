# cmake
cmake_minimum_required(VERSION 3.21)
include($ENV{XMOS_CMAKE_PATH}/xcommon.cmake)
project(VoicePipelineTest)

# root
set(XMOS_SANDBOX_DIR ${CMAKE_SOURCE_DIR}/sandbox)

# target
set(APP_HW_TARGET boards/XK_VOICE_L71.xn)
set(APP_XSCOPE_SRCS src/basic.xscope)

# Dependencies
set(APP_DEPENDENT_MODULES
    "lib_xcore_math(3.0.0)"
    "lib_voice(1.1.0)"
    "xscope_fileio(fb1ff81bb0b42c0425bea0ce009e8ee78dd9896d)"
)

set(APP_COMPILER_FLAGS
    -O3 -mno-dual-issue -g -report -Wall -mcmodel=large -Wno-xcore-fptrgroup
    -lquadflash
    -DALT_ARCH_MODE=1
)

XMOS_REGISTER_APP()
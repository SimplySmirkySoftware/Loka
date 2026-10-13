# Drives smirkycard_first_non_ascii_byte with a HEX string, so the cases need no
# binary fixture (CMake cannot write a NUL byte). Fails unless the first
# refused byte is at EXPECT_OFFSET (-1: none).
include("${LOKA_REPOSITORY_ROOT}/example/SmirkyCard/cmake/DiskScriptAscii.cmake")
smirkycard_first_non_ascii_byte("${HEX}" offset byte)
if(NOT offset EQUAL EXPECT_OFFSET)
  message(FATAL_ERROR "hex ${HEX}: first refused byte at ${offset} (0x${byte}), expected ${EXPECT_OFFSET}")
endif()

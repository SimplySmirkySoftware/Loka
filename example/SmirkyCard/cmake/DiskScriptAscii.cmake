# Classic LokaSimpleText opens 7-bit ASCII text only (TEXT_DOCUMENT_NON_ASCII),
# and the disk scripts exist to be edited there, so configure refuses any byte
# outside printable ASCII, tab, CR and LF.
#
# The scan reads the file as HEX and splits it into one list item per byte. A
# text read cannot see past an embedded NUL, and an anchored "(..)*" regex over
# the hex recurses once per byte and overflows CMake's stack on large files.

# Sets out_offset to the index of the first refused byte (-1 when none) and
# out_byte to its two lowercase hex digits.
function(smirkycard_first_non_ascii_byte hex out_offset out_byte)
  string(REGEX REPLACE "(..)" "\\1;" bytes "${hex}")
  set(refused ${bytes})
  list(FILTER refused INCLUDE REGEX "^(0[0-8bcef]|1[0-9a-f]|7f|[89a-f][0-9a-f])$")
  set(offset -1)
  set(byte "")
  if(refused)
    list(GET refused 0 byte)
    list(FIND bytes "${byte}" offset)
  endif()
  set(${out_offset} ${offset} PARENT_SCOPE)
  set(${out_byte} "${byte}" PARENT_SCOPE)
endfunction()

function(smirkycard_require_ascii_disk_script path name)
  file(READ "${path}" hex HEX)
  smirkycard_first_non_ascii_byte("${hex}" offset byte)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR
      "SmirkyCard disk script ${name} holds byte 0x${byte} at offset ${offset}; Classic LokaSimpleText could not open it")
  endif()
endfunction()

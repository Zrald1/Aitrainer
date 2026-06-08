# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\appAitrainer_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\appAitrainer_autogen.dir\\ParseCache.txt"
  "aimobile\\CMakeFiles\\appaimobile_autogen.dir\\AutogenUsed.txt"
  "aimobile\\CMakeFiles\\appaimobile_autogen.dir\\ParseCache.txt"
  "aimobile\\appaimobile_autogen"
  "appAitrainer_autogen"
  )
endif()

# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

cmake_policy(SET CMP0079 NEW)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("HTTPServer")

# Include sub-projects.
add_subdirectory ("HTTPServer")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "/usr/share/cmake-3.22/Modules/")

find_package(MySQL REQUIRED)

if (MYSQL_FOUND)
    message(STATUS "MySQL found")
    target_include_directories(HTTPServer PRIVATE ${MYSQL_INCLUDE_DIRS})
    target_link_libraries(HTTPServer PRIVATE ${MYSQL_LIBRARIES})
else ()
    message(FATAL_ERROR "MySQL not found")
endif ()
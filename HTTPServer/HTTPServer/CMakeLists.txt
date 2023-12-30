# CMakeList.txt : CMake project for HTTPServer, include source and define
# project specific logic here.
#

# Add source to this project's executable.
add_executable (HTTPServer "HTTPServer.cpp" "HTTPServer.h" "threadPool.h" "msgQueue.h"  "connection.h"  "sql.h" "session.h")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET HTTPServer PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.

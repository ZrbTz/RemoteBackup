cmake_minimum_required(VERSION 3.16)
project(pdsproject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS -pthread)

find_package(Boost 1.70.0 COMPONENTS system REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

set(OPENSSL_USE_STATIC_LIBS TRUE)
find_package(OpenSSL REQUIRED)

find_package(SQLite3 REQUIRED)
add_compile_options(-l sqlite3)


add_executable(Client Sources/Client/ClientMain.cpp Sources/Client/ClientEngine.cpp Headers/Client/ClientEngine.h Headers/General/Utils.h Sources/General/Utils.cpp Headers/General/Message.h Sources/General/Message.cpp Headers/Client/FileWatcher.h Sources/Client/FileWatcher.cpp)
target_link_libraries(Client ${Boost_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL ${SQLite3_LIBRARIES})

add_executable(Server Sources/Server/ServerMain.cpp Sources/Server/ServerEngine.cpp Headers/Server/ServerEngine.h Headers/General/Utils.h Sources/General/Utils.cpp Headers/Server/Database.h Sources/Server/Database.cpp Headers/General/Message.h Sources/General/Message.cpp)
target_link_libraries(Server ${Boost_LIBRARIES} OpenSSL::Crypto OpenSSL::SSL ${SQLite3_LIBRARIES})

if (CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(Client PUBLIC DEBUG=1 SERVER=0)
    target_compile_definitions(Server PUBLIC DEBUG=1 SERVER=1)
    message("-- Debug macro activated")
elseif (CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_definitions(Client PUBLIC DEBUG=0 SERVER=0)
    target_compile_definitions(Server PUBLIC DEBUG=0 SERVER=1)
    message("-- Debug macro deactivated")
endif ()

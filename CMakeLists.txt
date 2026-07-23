cmake_minimum_required(VERSION 3.21)
project(ProjetoZeta LANGUAGES CXX)

enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Let windres find icon.ico referenced from resources/app.rc
set(CMAKE_RC_FLAGS "--include-dir=${CMAKE_SOURCE_DIR}/resources")

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(VLC_SDK "${CMAKE_SOURCE_DIR}/vlc-sdk")

add_executable(ProjetoZeta WIN32
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/YtDlp.cpp
    src/YtDlp.h
    src/VlcPlayer.cpp
    src/VlcPlayer.h
    src/Settings.h
    resources/app.qrc
    resources/app.rc
)

target_include_directories(ProjetoZeta PRIVATE "${VLC_SDK}/include")
target_link_directories(ProjetoZeta PRIVATE "${VLC_SDK}/lib")
target_link_libraries(ProjetoZeta PRIVATE Qt6::Widgets Qt6::Network vlc vlccore)

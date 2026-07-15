cmake_minimum_required(VERSION 3.10)
project(SysNova)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static -static-libgcc -static-libstdc++")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Принудительная релизная сборка (максимальная оптимизация и минимальный размер)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -s")

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".lib" ${CMAKE_FIND_LIBRARY_SUFFIXES})

find_package(glfw3 REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/imgui)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends)

# Добавлен app.rc для вшивания иконки в .exe
add_executable(SysNova WIN32
    main.cpp
    app.rc
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_impl_glfw.cpp
    imgui/imgui_impl_opengl3.cpp
)

target_link_libraries(SysNova 
    advapi32 
    shell32 
    psapi 
    glfw3
    opengl32
    gdi32
    ole32
    uuid
)
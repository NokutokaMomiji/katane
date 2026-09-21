cmake_minimum_required(VERSION 3.21)

project(katane C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(KATANE_SOURCES
    src/Array.c
    src/Chunk.c
    src/Compiler.c
    src/Debug.c
    src/Exceptions.c
    src/HashMap.c
    src/Main.c
    src/Map.c
    src/Memory.c
    src/Native.c
    src/Object.c
    src/Platform.c
    src/Primitives.c
    src/Scanner.c
    src/Table.c
    src/Tutorial.c
    src/TypeDescriptor.c
    src/Utf8.c
    src/Utilities.c
    src/Value.c
    src/VM.c
)

add_executable(katane ${KATANE_SOURCES})
target_link_libraries(katane m)

target_include_directories(katane PRIVATE include)

if(MSVC)
    target_compile_options(katane PRIVATE /W4)
else()
    target_compile_options(katane PRIVATE -Wall)
endif()

target_compile_definitions(katane PRIVATE
    $<$<CONFIG:Debug>:DEBUG_TRACE_EXECUTION>
    $<$<CONFIG:Debug>:DEBUG_PRINT_CODE>
)

target_compile_options(katane PRIVATE
    $<$<AND:$<CONFIG:Debug>,$<NOT:$<C_COMPILER_ID:MSVC>>>:-g -O0>
    $<$<AND:$<CONFIG:Release>,$<NOT:$<C_COMPILER_ID:MSVC>>>:-O2>
)

target_link_options(katane PRIVATE
    $<$<AND:$<CONFIG:Release>,$<NOT:$<C_COMPILER_ID:MSVC>>>:-s>
)

if(MSVC)
    target_compile_options(katane PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2>
    )
    target_compile_definitions(katane PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )
else()
    target_compile_definitions(katane PRIVATE
        $<$<CONFIG:Release>:NDEBUG>
    )
endif()

set_target_properties(katane PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin"
)

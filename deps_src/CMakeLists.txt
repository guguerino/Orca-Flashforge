cmake_minimum_required(VERSION 3.13)
project(deps_src)

# Include all dependency subdirectories

# Header-only libraries (INTERFACE)
add_subdirectory(agg)
add_subdirectory(ankerl)
add_subdirectory(earcut)
add_subdirectory(fast_float)
add_subdirectory(nanosvg)
add_subdirectory(nlohmann)
add_subdirectory(stb_dxt)     # Header-only STB DXT compression library

# macOS: mimalloc replaces the system malloc for the whole process (malloc zone override).
# Built as a shared library that the application links first; see src/CMakeLists.txt.
# Set MIMALLOC_DISABLE_REDIRECT=1 in the environment to run without it (A/B measurements).
if (APPLE)
    set(MI_BUILD_SHARED  ON  CACHE BOOL "" FORCE)
    set(MI_BUILD_STATIC  OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT  OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
    set(MI_OVERRIDE      ON  CACHE BOOL "" FORCE)
    set(MI_OSX_ZONE      ON  CACHE BOOL "" FORCE)
    set(MI_OSX_INTERPOSE ON  CACHE BOOL "" FORCE)
    set(MI_USE_CXX       OFF CACHE STRING "" FORCE)
    add_subdirectory(mimalloc EXCLUDE_FROM_ALL)
endif ()

# Static libraries
add_subdirectory(Shiny)
add_subdirectory(admesh)
add_subdirectory(clipper)
add_subdirectory(clipper2)
add_subdirectory(expat)
add_subdirectory(glu-libtess)
add_subdirectory(hidapi)
add_subdirectory(hints)        # Hints library with utility executable
add_subdirectory(imgui)
add_subdirectory(imguizmo)
add_subdirectory(libigl)
add_subdirectory(libnest2d)
add_subdirectory(mcut)
add_subdirectory(md4c)
add_subdirectory(mdns)
add_subdirectory(miniz)
add_subdirectory(minilzo)
add_subdirectory(qhull)
add_subdirectory(qoi)
add_subdirectory(semver)      # Semver static library

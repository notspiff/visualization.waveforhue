project(visualization.spectrum)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(kodi REQUIRED)
find_package(CURL REQUIRED)

if(WIN32)
  find_package(DirectX REQUIRED)
  set(INCLUDES ${DIRECTX_INCLUDE_DIR})
  add_definitions(-D_WIN32PC -D_USRDLL)
  set(WAVEFORHUE_SOURCES src/Main.cpp
                         src/fft.cpp)
  set(DEPLIBS ${DIRECTX_D3DX9_LIBRARY})
else(WIN32)
  find_package(OpenGL)
  if(OPENGL_FOUND)
    find_package(GLEW REQUIRED)
    set(INCLUDES ${OpenGL_INCLUDE_DIR}
                 ${GLEW_INCLUDE_DIR})
    add_definitions(-DHAS_OPENGL)
    set(WAVEFORHUE_SOURCES src/Main.cpp
                           src/fft.cpp)
    set(DEPLIBS ${OPENGL_LIBRARIES} ${GLEW_LIBRARIES})
  else()
    find_package(OpenGLES2)
    if(OPENGLES2_FOUND)
      set(INCLUDES ${OPENGLES2_INCLUDE_DIR})
      add_definitions(-DHAS_GLES2)
      set(WAVEFORHUE_SOURCES src/Main_gles.cpp
                             src/fft.cpp
                             src/VisMatrixGLES.cpp
                             src/VisShader.cpp)
      set(DEPLIBS ${OPENGLES2_LIBRARIES})
    else()
      message(FATAL_ERROR "No rendering API found. Bailing")
    endif()
  endif()
endif()

include_directories(${INCLUDES}
                    ${CURL_INCLUDE_DIR}
                    ${KODI_INCLUDE_DIR})

list(APPEND DEPLIBS ${CURL_LIBRARIES})

build_addon(visualization.waveforhue WAVEFORHUE DEPLIBS)

include(CPack)

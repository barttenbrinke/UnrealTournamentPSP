project(UnrealTournament CXX)

set(SRC_FILES "Src/Launch.cpp")

if(TARGET_IS_DREAMCAST AND DREAMCAST_NO_WHOLE_ARCHIVE)
  list(APPEND SRC_FILES "Src/ForceLink.cpp")
endif()

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

# Select the appropriate viewport / window driver
if(USE_SDL)
  # SDL / NSDLDrv path (POSIX/Windows with SDL2)
  target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} NSDLDrv)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
elseif(TARGET_IS_DREAMCAST)
  # Native Dreamcast driver (KOSDrv) – added from the top‑level CMakeLists
  target_link_libraries(${PROJECT_NAME} KOSDrv)
else()
  # Non‑Windows, non‑Dreamcast platforms currently only support the SDL path
  message(FATAL_ERROR "Platforms other than Windows and Dreamcast can only use NSDLDrv.")
endif()


if(BUILD_STATIC)
  set(LINK_PACKAGES ${INSTALL_TARGETS})
  list(REMOVE_ITEM LINK_PACKAGES ${PROJECT_NAME})
  if(BUILD_DCUTIL)
    list(REMOVE_ITEM LINK_PACKAGES DCUtil)
  endif()
  if(TARGET_IS_DREAMCAST AND DREAMCAST_NO_WHOLE_ARCHIVE)
    target_link_libraries(${PROJECT_NAME} ${LINK_PACKAGES})
  else()
    target_link_libraries(${PROJECT_NAME} -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive)
  endif()
else()
  target_link_libraries(${PROJECT_NAME} Engine Core)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(TARGET_IS_DREAMCAST AND DREAMCAST_USE_FATFS)
  target_link_libraries(${PROJECT_NAME} fatfs)
endif()

if(PSP)
  # Mirrors `psp-pkg-config --libs sdl2`, plus pspgl and the C++ runtime.
  # SDL2 #defines main -> SDL_main, so SDL2main supplies the real entry point.
  target_link_libraries(${PROJECT_NAME}
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    SDL2main SDL2
    GLU GL
    xmp
    me-core pspvram pspaudio pspaudiolib pspvfpu pspdisplay pspgu pspge
    psphprm pspctrl psppower psprtc
    stdc++ m
  )
  # libxmp's memory must not share cache lines with the CPU heap (the music
  # runs on the Media Engine): NOpenALDrv wraps the allocator entry points.
  target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc)
  # MEMSIZE=1 in PARAM.SFO claims the PSP-2000's extra 32MB.
  create_pbp_file(
    TARGET ${PROJECT_NAME}
    TITLE "Unreal Tournament"
    VERSION 01.00
  )
endif()

# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)
# We also need PICO EXTRAS
set(PICO_EXTRAS_FETCH_FROM_GIT ON CACHE BOOL "Set to ON to fetch copy of PICO EXTRAS from git if not otherwise locatable")
include(pico_extras_import.cmake)

project(boumboum C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(pico_audio_i2s_32b/src/pico_audio_32b pico_audio_32b)
add_subdirectory(pico_audio_i2s_32b pico_audio_i2s_32b)

# Add executable. Default name is the project name, version 0.1

add_executable(boumboum 
	boumboum.cpp 
	coder.cpp 
	util.cpp 
	frequences.cpp 
	oscillator.cpp
	bb_i2s.cpp
	test.cpp)

pico_set_program_name(boumboum "boumboum")
pico_set_program_version(boumboum "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(boumboum 0)
pico_enable_stdio_usb(boumboum 1)

# Add the standard library to the build
target_link_libraries(boumboum
        pico_stdlib
		hardware_clocks
		hardware_dma
		hardware_pio
    	pico_audio_32b
    	pico_audio_i2s_32b
)

# Add the standard include files to the build
target_include_directories(boumboum PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Lien avec les bibliothèques nécessaires
#target_link_libraries(boumboum pico_stdlib hardware_pio hardware_clocks audio)

pico_add_extra_outputs(boumboum)

# add_executable(coder coder.cpp)
# target_link_libraries(coder pico_stdlib hardware_timer)

# pico_add_extra_outputs(coder)



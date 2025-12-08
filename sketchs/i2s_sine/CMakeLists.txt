
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# Import du SDK Pico
include(pico_sdk_import.cmake)

project(i2s_sine C CXX ASM)

# Initialisation du SDK
pico_sdk_init()

# --- Exécutable principal ---
add_executable(i2s_sine
    i2s_sine.cpp       # ton code C++ qui génère la sinusoïde
    i2s.pio        # programme PIO (sera compilé en .h automatiquement)
)

pico_enable_stdio_usb(i2s_sine 1)

# Add the standard library to the build
target_link_libraries(i2s_sine pico_stdlib)

# Génération automatique du header à partir du .pio
pico_generate_pio_header(i2s_sine ${CMAKE_CURRENT_LIST_DIR}/i2s.pio)

# Lien avec les bibliothèques nécessaires
target_link_libraries(i2s_sine pico_stdlib hardware_pio hardware_clocks)

# Génère les fichiers UF2, bin, hex
pico_add_extra_outputs(i2s_sine)


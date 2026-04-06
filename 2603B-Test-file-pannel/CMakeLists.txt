cmake_minimum_required(VERSION 3.20)
project(Test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/../3rdParty")

set(IMGUI_DIR "${THIRD_PARTY_DIR}/imgui")
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

set(APP_HEADERS
    "${CMAKE_SOURCE_DIR}/Application.h"
    "${CMAKE_SOURCE_DIR}/Window.h"
)

set(SHADER_FILES
    "${CMAKE_SOURCE_DIR}/shader.vert"
    "${CMAKE_SOURCE_DIR}/shader.frag"
)

add_executable(${PROJECT_NAME}
    main.cpp
    Application.cpp
    Window.cpp
    ${APP_HEADERS}
    ${SHADER_FILES}
    ${IMGUI_SOURCES}
)

source_group("Include Files" FILES ${APP_HEADERS})
source_group("Shader Files" FILES ${SHADER_FILES})
source_group("Imgui Files" FILES ${IMGUI_SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)
target_link_options(${PROJECT_NAME} PRIVATE /ignore:4099)
target_link_options(${PROJECT_NAME} PRIVATE /ignore:4819)

find_package(Vulkan REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS} 
    "${THIRD_PARTY_DIR}/glm"
    "${THIRD_PARTY_DIR}/eigen"
    "${THIRD_PARTY_DIR}/GLFW/include"
    "${THIRD_PARTY_DIR}/OpenMesh/src"
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_directories(Test PRIVATE
    "${THIRD_PARTY_DIR}/lib"
    "$ENV{VULKAN_SDK}/Lib"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    debug     glfw3d.lib            optimized   glfw3.lib
    debug     glmd.lib              optimized   glm.lib
    debug     OpenMeshCored.lib     optimized   OpenMeshCore.lib
    debug     shaderc_combinedd.lib optimized   shaderc_combined.lib
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Comdlg32)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


#
# Windows system libs required by GLFW
#if(WIN32)
#    target_link_libraries(${PROJECT_NAME} PRIVATE gdi32 user32 shell32)
#endif()


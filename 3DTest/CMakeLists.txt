cmake_minimum_required(VERSION 3.20)
project(Test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/../3rdParty")

add_executable(${PROJECT_NAME} main.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)
target_link_options(${PROJECT_NAME} PRIVATE /ignore:4099)
target_link_options(${PROJECT_NAME} PRIVATE /ignore:4819)

find_package(Vulkan REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${Vulkan_INCLUDE_DIRS} 
    "${THIRD_PARTY_DIR}/glm",
    "${THIRD_PARTY_DIR}/eigen",
    "${THIRD_PARTY_DIR}/GLFW/include",
    "${THIRD_PARTY_DIR}/OpenMesh/src",
)

target_link_directories(Test PRIVATE
    "${THIRD_PARTY_DIR}/lib",
    "$ENV{VULKAN_SDK}/Lib",
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    debug     glfw3d.lib            optimized   glfw3.lib
    debug     glmd.lib              optimized   glm.lib
    debug     OpenMeshCored.lib     optimized   OpenMeshCore.lib
    debug     shaderc_combinedd.lib optimized   shaderc_combined.lib
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)


#
# Windows system libs required by GLFW
#if(WIN32)
#    target_link_libraries(${PROJECT_NAME} PRIVATE gdi32 user32 shell32)
#endif()


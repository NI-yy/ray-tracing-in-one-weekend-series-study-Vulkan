cmake_minimum_required(VERSION 3.25)
project(RayTracing)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(USE_VULKAN "Enable Vulkan compute renderer support" ON)

add_executable(inOneWeekend
    src/main.cpp
    src/vec3.h
    src/color.h
    src/ray.h
    src/hittable.h
    src/sphere.h
    src/hittable_list.h
    src/rtweekend.h
    src/interval.h
    src/camera.h
    src/material.h
)

if(USE_VULKAN)
    find_package(Vulkan REQUIRED)
    include(${CMAKE_SOURCE_DIR}/cmake/CompileShaders.cmake)

    set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
    set(SHADER_GRADIENT ${SHADER_DIR}/gradient.comp)
    set(SHADER_SPHERE ${SHADER_DIR}/sphere.comp)
    set(SHADER_SPHERES ${SHADER_DIR}/spheres.comp)
    set(SHADER_DIFFUSE ${SHADER_DIR}/diffuse.comp)
    set(SHADER_MATERIALS ${SHADER_DIR}/materials.comp)

    compile_shader(${SHADER_GRADIENT} COMPILED_GRADIENT)
    compile_shader(${SHADER_SPHERE} COMPILED_SPHERE)
    compile_shader(${SHADER_SPHERES} COMPILED_SPHERES)
    compile_shader(${SHADER_DIFFUSE} COMPILED_DIFFUSE)
    compile_shader(${SHADER_MATERIALS} COMPILED_MATERIALS)

    target_sources(inOneWeekend PRIVATE
        src/vulkan_renderer.cpp
        src/vulkan_renderer.h
        ${SHADER_GRADIENT}
        ${COMPILED_GRADIENT}
        ${SHADER_SPHERE}
        ${COMPILED_SPHERE}
        ${SHADER_SPHERES}
        ${COMPILED_SPHERES}
        ${SHADER_DIFFUSE}
        ${COMPILED_DIFFUSE}
        ${SHADER_MATERIALS}
        ${COMPILED_MATERIALS}
    )
    target_compile_definitions(inOneWeekend PRIVATE RTWEEKEND_VULKAN_ENABLED)
    target_link_libraries(inOneWeekend PRIVATE Vulkan::Vulkan)
endif()

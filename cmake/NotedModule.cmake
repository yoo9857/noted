# Helper to declare a noted module library with consistent settings.
#
# noted_add_module(
#     NAME    color
#     SOURCES src/color.cpp src/icc.cpp
#     PUBLIC_DEPS  Vulkan::Vulkan
#     PRIVATE_DEPS noted::error
# )
#
# Produces target `noted_<name>` and alias `noted::<name>`.
# Public headers are expected under include/noted/<name>/.
function(noted_add_module)
    set(options STATIC SHARED INTERFACE)
    set(oneValueArgs NAME)
    set(multiValueArgs SOURCES PUBLIC_DEPS PRIVATE_DEPS PUBLIC_DEFINES PRIVATE_DEFINES)
    cmake_parse_arguments(MOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT MOD_NAME)
        message(FATAL_ERROR "noted_add_module: NAME is required")
    endif()

    set(target "noted_${MOD_NAME}")

    if(MOD_INTERFACE)
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        )
        if(MOD_PUBLIC_DEPS)
            target_link_libraries(${target} INTERFACE ${MOD_PUBLIC_DEPS})
        endif()
    else()
        if(NOT MOD_SOURCES)
            # Header-only-with-stub: create empty TU so the lib has an object file.
            set(_stub "${CMAKE_CURRENT_BINARY_DIR}/${MOD_NAME}_stub.cpp")
            file(WRITE "${_stub}" "// auto-generated stub for noted_${MOD_NAME}\nnamespace { [[maybe_unused]] int _stub = 0; }\n")
            set(MOD_SOURCES "${_stub}")
        endif()
        if(MOD_SHARED)
            add_library(${target} SHARED ${MOD_SOURCES})
        else()
            add_library(${target} STATIC ${MOD_SOURCES})
        endif()
        target_include_directories(${target}
            PUBLIC
                $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:include>
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_compile_features(${target} PUBLIC cxx_std_23)
        if(MOD_PUBLIC_DEPS)
            target_link_libraries(${target} PUBLIC ${MOD_PUBLIC_DEPS})
        endif()
        if(MOD_PRIVATE_DEPS)
            target_link_libraries(${target} PRIVATE ${MOD_PRIVATE_DEPS})
        endif()
        if(MOD_PUBLIC_DEFINES)
            target_compile_definitions(${target} PUBLIC ${MOD_PUBLIC_DEFINES})
        endif()
        if(MOD_PRIVATE_DEFINES)
            target_compile_definitions(${target} PRIVATE ${MOD_PRIVATE_DEFINES})
        endif()

        noted_apply_warnings(${target})
        noted_apply_hardening(${target})
    endif()

    add_library(noted::${MOD_NAME} ALIAS ${target})
endfunction()

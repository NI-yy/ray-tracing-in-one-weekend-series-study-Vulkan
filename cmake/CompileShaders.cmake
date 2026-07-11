function(compile_shader SOURCE_FILE OUT_VAR)
    set(options)
    set(oneValueArgs OUTPUT_NAME)
    set(multiValueArgs DEFINES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(FILENAME ${SOURCE_FILE} NAME)

    if(ARG_OUTPUT_NAME)
        get_filename_component(SOURCE_DIR ${SOURCE_FILE} DIRECTORY)
        set(OUTPUT_FILE "${SOURCE_DIR}/${ARG_OUTPUT_NAME}")
    else()
        set(OUTPUT_FILE "${SOURCE_FILE}.spv")
    endif()

    set(COMMAND_ARGS -V ${SOURCE_FILE} --target-env vulkan1.3 -o ${OUTPUT_FILE})

    foreach(DEFINE IN LISTS ARG_DEFINES)
        list(APPEND COMMAND_ARGS -D${DEFINE})
    endforeach()

    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND glslangValidator ${COMMAND_ARGS}
        DEPENDS ${SOURCE_FILE}
        COMMENT "Compiling shader: ${FILENAME} -> ${OUTPUT_FILE}"
        VERBATIM
    )

    set(${OUT_VAR} ${OUTPUT_FILE} PARENT_SCOPE)
endfunction()

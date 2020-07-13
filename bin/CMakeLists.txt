macro(copy_bin_dir dir)
    file(GLOB_RECURSE SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*")
    # Walk over all the bin files, and generate a command to copy them
    foreach(SOURCE_FILE IN LISTS SOURCE_FILES)
        get_filename_component(FILE_NAME "${SOURCE_FILE}" NAME)
        set(BINARY_FILE "${CMAKE_BINARY_DIR}/${dir}/${FILE_NAME}")

        add_custom_command(OUTPUT ${BINARY_FILE}
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${SOURCE_FILE}
                        ${BINARY_FILE}
                MAIN_DEPENDENCY ${SOURCE_FILE}
                COMMENT "Copying ${FILE_NAME} bin/${dir}/ file"
        )

        list(APPEND BIN_BINARY_FILES ${BINARY_FILE})
    endforeach()
endmacro()

copy_bin_dir(ai)
copy_bin_dir(game)
copy_bin_dir(scripts)

# Create a new target which generates all bin files
add_custom_target_timestamp(binfiles_files
        DEPENDS
        ${BIN_BINARY_FILES}
)

add_library(binfiles
        INTERFACE
)
add_dependencies(binfiles
        binfiles_files
)
add_library(openttd::binfiles ALIAS binfiles)

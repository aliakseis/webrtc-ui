find_package(Git QUIET)

get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}" DIRECTORY)

set(VERSION_FILE "${SOURCE_DIR}/version.h")
set(TMP_FILE "${VERSION_FILE}.tmp")

if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE GIT_RESULT
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(GIT_FOUND AND GIT_RESULT EQUAL 0)
    file(WRITE "${TMP_FILE}" "#define GIT_COMMIT ${GIT_COMMIT}\n")
else()
    file(WRITE "${TMP_FILE}" "#define NO_GIT_COMMIT\n")
endif()

set(UPDATE_FILE TRUE)

if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" OLD_CONTENT)
    file(READ "${TMP_FILE}" NEW_CONTENT)

    if(OLD_CONTENT STREQUAL NEW_CONTENT)
        set(UPDATE_FILE FALSE)
    endif()
endif()

if(UPDATE_FILE)
    file(RENAME "${TMP_FILE}" "${VERSION_FILE}")
else()
    file(REMOVE "${TMP_FILE}")
endif()

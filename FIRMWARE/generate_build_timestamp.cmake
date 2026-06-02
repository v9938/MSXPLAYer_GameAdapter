if(NOT DEFINED OUTPUT_FILE)
    message(FATAL_ERROR "OUTPUT_FILE is not defined")
endif()

if(NOT DEFINED SOURCE_FILES)
    message(FATAL_ERROR "SOURCE_FILES is not defined")
endif()

set(LATEST_FILE "")
set(LATEST_TIMESTAMP "")

foreach(SRC IN LISTS SOURCE_FILES)
    if(EXISTS "${SRC}")
        if(WIN32)
            execute_process(
                COMMAND powershell -NoProfile -Command "(Get-Item '${SRC}').LastWriteTime.ToString('yyyyMMddHHmm')"
                OUTPUT_VARIABLE FILE_TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        else()
            execute_process(
                COMMAND date -r "${SRC}" "+%Y%m%d%H%M"
                OUTPUT_VARIABLE FILE_TIMESTAMP
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        endif()

        if(FILE_TIMESTAMP)
            if(LATEST_TIMESTAMP STREQUAL "" OR FILE_TIMESTAMP GREATER LATEST_TIMESTAMP)
                set(LATEST_TIMESTAMP "${FILE_TIMESTAMP}")
                set(LATEST_FILE "${SRC}")
            endif()
        endif()
    endif()
endforeach()

if(LATEST_TIMESTAMP STREQUAL "")
    string(TIMESTAMP LATEST_TIMESTAMP "%Y%m%d%H%M")
endif()

file(WRITE "${OUTPUT_FILE}"
"#ifndef BUILD_TIMESTAMP_H\n"
"#define BUILD_TIMESTAMP_H\n"
"#define PROJECT_VERSION \"${LATEST_TIMESTAMP}\"\n"
"#endif\n"
)

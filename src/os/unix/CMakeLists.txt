add_files(
    crashlog_unix.cpp
    survey_unix.cpp
    CONDITION UNIX AND NOT APPLE
)

add_files(
    unix.cpp
    CONDITION UNIX
)

add_files(
    font_unix.cpp
    CONDITION Fontconfig_FOUND
)

if (MSVC)
    # Disable warning 4244 for monocypher.cpp
    set_compile_flags(monocypher.cpp COMPILE_FLAGS /wd4244)
endif()

add_files(
    monocypher-ed25519.cpp
    monocypher-ed25519.h
    monocypher.cpp
    monocypher.h
)

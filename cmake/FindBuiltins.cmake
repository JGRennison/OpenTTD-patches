include(CheckCXXSourceCompiles)

check_cxx_source_compiles("
    int main() {
        return __builtin_popcountll(__builtin_popcountl(__builtin_popcount(__builtin_ctz(1))));
    }"
    BITMATH_BUILTINS_FOUND
)

if (BITMATH_BUILTINS_FOUND)
    add_compile_options(
        -DWITH_BITMATH_BUILTINS
    )
endif (BITMATH_BUILTINS_FOUND)

check_cxx_source_compiles("
#include <cstdint>
int main() {
    int64_t a = 0;
    int64_t b = 0;
    int64_t c = 0;
    bool res1 = __builtin_add_overflow(a, b, &c);
    bool res2 = __builtin_sub_overflow(a, b, &c);
    bool res3 = __builtin_mul_overflow(a, b, &c);
    return (res1 || res2 || res3) ? 1 : 0;
}"
    OVERFLOW_BUILTINS_FOUND
)

if (OVERFLOW_BUILTINS_FOUND)
    add_compile_options(
        -DWITH_OVERFLOW_BUILTINS
    )
endif (OVERFLOW_BUILTINS_FOUND)

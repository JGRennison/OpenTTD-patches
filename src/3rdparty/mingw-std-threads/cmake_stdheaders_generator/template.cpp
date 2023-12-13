#ifdef MINGW_STDTHREADS_DETECTING_SYSTEM_HEADER 
    #include MINGW_STDTHREADS_DETECTING_SYSTEM_HEADER
    static_assert(false, "Prevent compilation")
#else
    #pragma once 
    // both system header and mignw-stdthreads header should already have include
    // guards. But we still add a #pragma once just to be safe.
    
    #include "${mingw_stdthreads_headers_generator_system_header}"
    #include "${mingw_stdthreads_headers_generator_library_header}"
#endif
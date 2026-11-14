option(VLOOM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Named for this project, so it does not clash with vloom-engine's function
# when the engine adds vloom-dsp as a subproject.
function(vloom_dsp_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /FS)
        if(VLOOM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
        if(VLOOM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

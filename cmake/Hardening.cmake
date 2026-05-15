# Security and runtime hardening flags.
# Usage:  noted_apply_hardening(<target>)
function(noted_apply_hardening target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /GS                            # buffer security check
            /guard:cf                      # control flow guard
            $<$<CONFIG:Release>:/sdl>      # additional security checks
        )
        target_link_options(${target} PRIVATE
            /guard:cf
            /DYNAMICBASE                   # ASLR
            /NXCOMPAT                      # DEP
            $<$<CONFIG:Release>:/HIGHENTROPYVA>
        )
    else()
        target_compile_options(${target} PRIVATE
            -fstack-protector-strong
            -fstack-clash-protection
            -fcf-protection=full
            -D_FORTIFY_SOURCE=2
            -fPIE
        )
        target_link_options(${target} PRIVATE
            -Wl,-z,relro
            -Wl,-z,now
            -Wl,-z,noexecstack
            -pie
        )
    endif()

    if(NOTED_ENABLE_ASAN)
        if(MSVC)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        else()
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
    endif()
    if(NOTED_ENABLE_UBSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=undefined)
        target_link_options(${target} PRIVATE -fsanitize=undefined)
    endif()
    if(NOTED_ENABLE_TSAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=thread)
        target_link_options(${target} PRIVATE -fsanitize=thread)
    endif()
endfunction()

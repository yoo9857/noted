# libFuzzer entry points for image decoders, document parsers, plugin ABI.
# Enabled only when building under clang with -fsanitize=fuzzer.
add_custom_target(noted_fuzz_placeholder
    COMMENT "fuzz: placeholder — libFuzzer targets land per-decoder")

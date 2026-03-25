.section .rodata,"",@
    .align 4
    .global _binary_ip_strings_bin_start
    .type _binary_ip_strings_bin_start, @object
_binary_ip_strings_bin_start:
    .incbin "data/ip_strings.bin"

    .global _binary_ip_strings_bin_end
    .type _binary_ip_strings_bin_end, @object
_binary_ip_strings_bin_end:
    .size _binary_ip_strings_bin_end, 0

    .size _binary_ip_strings_bin_start, _binary_ip_strings_bin_end - _binary_ip_strings_bin_start

    .global _binary_ip_strings_bin_size
    .type _binary_ip_strings_bin_size, @object
    .section .rodata.size,"",@
_binary_ip_strings_bin_size:
    .int32 _binary_ip_strings_bin_end - _binary_ip_strings_bin_start
    .size _binary_ip_strings_bin_size, 4

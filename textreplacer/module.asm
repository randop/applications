;# -*- mode: assembly; indent-tabs-mode: nil; sh-basic-offset: 4; -*-
;# ex: ts=8 sw=4 sts=4 et filetype=asm
;#
;#  module.asm
;#
;#  Copyright © 2010 — 2025 Randolph Ledesma
;#
;# Licensed under the Apache License, Version 2.0 (the "License");
;# you may not use this file except in compliance with the License.
;# You may obtain a copy of the License at
;#
;#    http://www.apache.org/licenses/LICENSE-2.0
;#
;# Unless required by applicable law or agreed to in writing, software
;# distributed under the License is distributed on an "AS IS" BASIS,
;# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
;# See the License for the specific language governing permissions and
;# limitations under the License.
;#

section .data
    buffer_size equ 4096
    error_open db "Error opening file", 10
    error_open_len equ $ - error_open
    error_write db "Error writing to file", 10
    error_write_len equ $ - error_write
    error_memory db "Memory allocation failed", 10
    error_memory_len equ $ - error_memory
    error_args db "Invalid arguments", 10
    error_args_len equ $ - error_args
    temp_file db "temp.txt", 0
    newline db 10
    str: db "textreplacer v0.1", 0xA
    STRSIZE: equ $ - str
    STDOUT: equ 1

section .bss
    input_fd resq 1
    output_fd resq 1
    filename resq 1
    buffer resb buffer_size
    file_content resq 1
    content_size resq 1
    find_str resq 1
    replace_str resq 1
    find_len resq 1
    replace_len resq 1
    replaced resb 1
    file_mode resd 1         ; To store file permissions

section .text
global _start

_start:
    mov rax, 1
    mov rdi, STDOUT
    mov rsi, str
    mov rdx, STRSIZE
    syscall

    ; Check if correct number of arguments (3: filename, find, replace)
    mov rax, [rsp]
    cmp rax, 4
    jne error_arguments

    ; Get arguments
    mov rsi, [rsp + 16]      ; argv[1] - filename
    mov [filename], rsi
    mov rsi, [rsp + 24]      ; argv[2] - find text
    mov [find_str], rsi
    mov rsi, [rsp + 32]      ; argv[3] - replace text
    mov [replace_str], rsi

    ; Check filename length
    mov rsi, [filename]
    call strlen
    cmp rax, 0
    je error_arguments

    ; Check find_str length
    mov rsi, [find_str]
    call strlen
    mov [find_len], rax
    cmp rax, 0
    je error_arguments

    ; Check replace_str length
    mov rsi, [replace_str]
    call strlen
    mov [replace_len], rax

    ; Open input file
    mov rax, 2               ; sys_open
    mov rdi, [filename]
    mov rsi, 0               ; O_READONLY
    syscall
    cmp rax, 0
    jl error_open_file
    mov [input_fd], rax

    ; Get file permissions
    mov rax, 5               ; sys_fstat
    mov rdi, [input_fd]
    sub rsp, 144             ; stat buffer
    mov rsi, rsp
    syscall
    mov eax, [rsp + 24]      ; st_mode from stat
    mov [file_mode], eax
    add rsp, 144

    ; Read file content
    call read_file
    cmp rax, 0
    jl error_open_file

    ; Create temporary file
    mov rax, 2               ; sys_open
    mov rdi, temp_file
    mov rsi, 577             ; O_WRONLY | O_CREAT | O_TRUNC
    mov rdx, [file_mode]     ; Use original file permissions
    syscall
    cmp rax, 0
    jl error_open_file
    mov [output_fd], rax

    ; Process file content (replace text)
    call replace_text

    ; Write to temp file
    mov rax, 1               ; sys_write
    mov rdi, [output_fd]
    mov rsi, [file_content]
    mov rdx, [content_size]
    syscall
    cmp rax, 0
    jl error_write_file

    ; Close files
    mov rax, 3               ; sys_close
    mov rdi, [input_fd]
    syscall
    mov rax, 3
    mov rdi, [output_fd]
    syscall

    ; Rename temp file to original
    mov rax, 82              ; sys_rename
    mov rdi, temp_file
    mov rsi, [filename]
    syscall
    cmp rax, 0
    jl error_open_file

    ; Free memory
    mov rax, 11              ; sys_munmap
    mov rdi, [file_content]
    mov rsi, [content_size]
    syscall

    ; Exit with appropriate code (0 if replaced, 1 if no replacements)
    movzx rax, byte [replaced]
    xor rax, 1               ; Invert: 1->0 (success), 0->1 (no replacements)
    mov rdi, rax
    mov rax, 60              ; sys_exit
    syscall

strlen:
    xor rax, rax
    .loop:
        cmp byte [rsi + rax], 0
        je .done
        inc rax
        jmp .loop
    .done:
        ret

read_file:
    ; Get file size
    mov rax, 5               ; sys_fstat
    mov rdi, [input_fd]
    sub rsp, 144             ; stat buffer
    mov rsi, rsp
    syscall
    mov rbx, [rsp + 48]      ; file size from stat
    add rsp, 144

    ; Allocate memory
    mov rax, 9               ; sys_mmap
    xor rdi, rdi             ; addr = NULL
    mov rsi, rbx             ; length
    mov rdx, 3               ; PROT_READ | PROT_WRITE
    mov r10, 34              ; MAP_PRIVATE | MAP_ANONYMOUS
    mov r8, -1               ; fd = -1
    xor r9, r9               ; offset = 0
    syscall
    cmp rax, -1
    je error_memory_alloc
    mov [file_content], rax
    mov [content_size], rbx

    ; Read file
    xor r12, r12             ; total bytes read
    .read_loop:
        mov rax, 0           ; sys_read
        mov rdi, [input_fd]
        mov rsi, buffer
        mov rdx, buffer_size
        syscall
        cmp rax, 0
        jle .read_done
        ; Copy to memory
        mov rdi, [file_content]
        add rdi, r12
        mov rsi, buffer
        mov rcx, rax
        rep movsb
        add r12, rax
        jmp .read_loop
    .read_done:
        mov [content_size], r12
        ret

replace_text:
    mov byte [replaced], 0
    mov r12, [file_content]
    mov r13, [content_size]
    xor r14, r14             ; position in file

    .search_loop:
        cmp r14, r13
        jge .done
        mov rsi, [find_str]
        mov rdi, r12
        add rdi, r14
        mov rcx, [find_len]
        call strncmp
        cmp rax, 0
        je .found
        inc r14
        jmp .search_loop

    .found:
        mov byte [replaced], 1
        ; Shift content if replace_len != find_len
        mov rax, [replace_len]
        mov rbx, [find_len]
        cmp rax, rbx
        jne .resize
        ; Simple replacement
        mov rsi, [replace_str]
        mov rdi, r12
        add rdi, r14
        mov rcx, [replace_len]
        rep movsb
        add r14, [find_len]
        jmp .search_loop

    .resize:
        ; Allocate new buffer
        mov rax, [content_size]
        add rax, [replace_len]
        sub rax, [find_len]
        mov rsi, rax
        mov rax, 9           ; sys_mmap
        xor rdi, rdi
        mov rdx, 3           ; PROT_READ | PROT_WRITE
        mov r10, 34          ; MAP_PRIVATE | MAP_ANONYMOUS
        mov r8, -1
        xor r9, r9
        syscall
        cmp rax, -1
        je error_memory_alloc
        mov r15, rax         ; new buffer

        ; Copy content up to match
        mov rdi, r15
        mov rsi, r12
        mov rcx, r14
        rep movsb

        ; Copy replacement
        mov rsi, [replace_str]
        mov rcx, [replace_len]
        rep movsb

        ; Copy remaining content
        mov rsi, r12
        add rsi, r14
        add rsi, [find_len]
        mov rcx, [content_size]
        sub rcx, r14
        sub rcx, [find_len]
        rep movsb

        ; Update content
        mov rax, 11          ; sys_munmap
        mov rdi, [file_content]
        mov rsi, [content_size]
        syscall
        mov [file_content], r15
        mov rax, [content_size]
        add rax, [replace_len]
        sub rax, [find_len]
        mov [content_size], rax
        mov r12, r15
        mov r13, rax
        add r14, [replace_len]
        jmp .search_loop

    .done:
        ret

strncmp:
    xor rax, rax
    .loop:
        cmp rcx, 0
        je .equal
        mov al, [rdi]
        cmp al, [rsi]
        jne .not_equal
        inc rdi
        inc rsi
        dec rcx
        jmp .loop
    .equal:
        xor rax, rax
        ret
    .not_equal:
        mov rax, 1
        ret

error_arguments:
    mov rax, 1
    mov rdi, 2
    mov rsi, error_args
    mov rdx, error_args_len
    syscall
    mov rax, 60
    mov rdi, 1               ; Exit code 1 for argument errors
    syscall

error_open_file:
    mov rax, 1
    mov rdi, 2
    mov rsi, error_open
    mov rdx, error_open_len
    syscall
    mov rax, 60
    mov rdi, 2               ; Exit code 2 for file errors
    syscall

error_write_file:
    mov rax, 1
    mov rdi, 2
    mov rsi, error_write
    mov rdx, error_write_len
    syscall
    mov rax, 60
    mov rdi, 2               ; Exit code 2 for file errors
    syscall

error_memory_alloc:
    mov rax, 1
    mov rdi, 2
    mov rsi, error_memory
    mov rdx, error_memory_len
    syscall
    mov rax, 60
    mov rdi, 3               ; Exit code 3 for memory errors
    syscall

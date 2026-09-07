    .intel_syntax noprefix
    .text
    .globl oot3d_source_call_on_stack
    .def oot3d_source_call_on_stack; .scl 2; .type 32; .endef

# rcx = inclusive stack limit, rdx = exclusive stack base
# r8 = noexcept trampoline, r9 = trampoline context
oot3d_source_call_on_stack:
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    mov r13, QWORD PTR gs:0x08
    mov r14, QWORD PTR gs:0x10
    mov r15, r8

    mov QWORD PTR gs:0x08, rdx
    mov QWORD PTR gs:0x10, rcx
    mov rcx, r9
    mov rsp, rdx
    and rsp, -16
    sub rsp, 32
    call r15

    mov QWORD PTR gs:0x08, r13
    mov QWORD PTR gs:0x10, r14
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    ret

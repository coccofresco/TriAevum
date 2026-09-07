OPTION PROLOGUE:NONE
OPTION EPILOGUE:NONE

.code

PUBLIC oot3d_source_call_on_stack

; rcx = inclusive stack limit, rdx = exclusive stack base
; r8 = noexcept trampoline, r9 = trampoline context
oot3d_source_call_on_stack PROC
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    mov r13, gs:[08h]
    mov r14, gs:[10h]
    mov r15, r8

    mov gs:[08h], rdx
    mov gs:[10h], rcx
    mov rcx, r9
    mov rsp, rdx
    and rsp, -16
    sub rsp, 32
    call r15

    mov gs:[08h], r13
    mov gs:[10h], r14
    mov rsp, r12
    pop r15
    pop r14
    pop r13
    pop r12
    ret
oot3d_source_call_on_stack ENDP

END

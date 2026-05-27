/*
 * crt0.S — userspace startup. Kernel sets up the stack as:
 *   esp+0  ret addr (0)   esp+4  argc   esp+8  argv   esp+12  envp
 * Calls main(argc, argv, envp), passes its return value to _exit.
 */

.section .text
.global _start
.type _start, @function

_start:
    xorl %ebp, %ebp             /* end of frame-pointer chain for debuggers */

    movl 4(%esp), %eax
    movl 8(%esp), %ebx
    movl 12(%esp), %ecx

    pushl %ecx                  /* envp */
    pushl %ebx                  /* argv */
    pushl %eax                  /* argc */
    call main

    addl $12, %esp
    pushl %eax                  /* exit status = main()'s return */
    call _exit

    hlt                         /* unreachable */

.size _start, . - _start

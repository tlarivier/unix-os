/*
 * crt0.S - C Runtime startup for userspace programs (pure assembly)
 * 
 * Stack layout on entry (set by kernel):
 *   esp+0:  return address (0)
 *   esp+4:  argc
 *   esp+8:  argv
 *   esp+12: envp
 *
 * Calls: int main(int argc, char **argv, char **envp)
 * Then:  void _exit(int status)
 */

.section .text
.global _start
.type _start, @function

_start:
    /* Clear frame pointer for debuggers */
    xorl %ebp, %ebp
    
    /* Get argc, argv, envp from stack */
    movl 4(%esp), %eax      /* argc */
    movl 8(%esp), %ebx      /* argv */
    movl 12(%esp), %ecx     /* envp */
    
    /* Push arguments for main(argc, argv, envp) */
    pushl %ecx              /* envp */
    pushl %ebx              /* argv */
    pushl %eax              /* argc */
    
    /* Call main */
    call main
    
    /* main returned - call _exit with return value */
    addl $12, %esp          /* clean up main args */
    pushl %eax              /* push return value as exit status */
    call _exit
    
    /* Should never reach here */
    hlt
    
.size _start, . - _start

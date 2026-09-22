set pagination off
set confirm off
set can-use-hw-watchpoints 1
target remote :1234
file build/kernel.elf
break sched_preempt_pick
continue
disable 1
printf "=== 1a preempcao armada; watchpoint em 0x100000 ===\n"
watch *0x100000
continue
printf "=== WRITER DE 0x100000 ENCONTRADO ===\n"
info registers rip rsp rbp rcx rbx rax rdx rdi rsi r8 r9 r10 r11
x/16gx 0x100000
bt
detach
quit
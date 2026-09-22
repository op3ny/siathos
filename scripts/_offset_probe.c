#include "../src/kernel/include/thais.h"
#include <stdio.h>
#include <stddef.h>
int main(void){
    printf("ctx     = 0x%lx\n", offsetof(process_t, ctx));
    printf("ctx.rip = 0x%lx\n", offsetof(process_t, ctx.rip));
    printf("ctx.rsp = 0x%lx\n", offsetof(process_t, ctx.rsp));
    printf("ctx.cr3 = 0x%lx\n", offsetof(process_t, ctx.cr3));
    printf("ctx.ran = 0x%lx\n", offsetof(process_t, ctx.ran));
    printf("ctx.style=0x%lx\n", offsetof(process_t, ctx.style));
    printf("ctx.hw  = 0x%lx\n", offsetof(process_t, ctx.hw_rsp));
    printf("sizeof ctx=%lu proc=%lu\n", (unsigned long)sizeof(cpu_context_t), (unsigned long)sizeof(process_t));
    return 0;
}
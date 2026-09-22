/* hello - app userspace de exemplo (MARCO 1) em ring 3.
   Compilado FORA do kernel (gcc -> ELF64), embutido via hello_elf.h e
   executado por exec_elf (proc_create_user_regions) em anel 3 com argc/argv.
   Mantem o comportamento do hello anterior: "[hello] args:" + argv. */
#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT  0
#define SYS_WRITE 1

static long hello_syscall(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3){
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(nr), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "memory");
    return r;
}

static void hello_write(const char *s){
    size_t n=0;
    while(s[n]) n++;
    hello_syscall(SYS_WRITE, 1, (uint64_t)(uintptr_t)s, n);
}

int main(int argc, char **argv){
    hello_write("[hello] args:");
    for(int i=0;i<argc && argv && argv[i];i++){
        hello_write(" ");
        hello_write(argv[i]);
    }
    hello_write("\n");
    hello_syscall(SYS_EXIT, 0, 0, 0);
    return 0;
}
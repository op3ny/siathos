/* mem - mostra memoria (MARCO 5) - app userspace via spud.h.
   Uso: mem | aisthesis. Retorna 0. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

int main(int argc, char **argv){
    (void)argc;
    spud_sysinfo_t si;
    long r=spud_sys(SYS_SYSINFO, (uint64_t)(uintptr_t)&si, 0, 0, 0, 0);
    if(r!=0){ spud_write("mem: erro\n"); return 1; }
    spud_write("  total: "); spud_print_dec(si.memory_total); spud_write("B\n");
    spud_write("  usado: "); spud_print_dec(si.memory_used);  spud_write("B\n");
    spud_write("  livre: "); spud_print_dec(si.memory_free);  spud_write("B\n");
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}

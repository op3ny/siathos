#include "thais.h"
#include <stdbool.h>

/* sysinfo.c — informacoes do sistema (ABI 1.0).
   Preenche system_info_t/proc_info_t a partir do estado do kernel
   (PMM, scheduler, PIT). Usado pelo syscall SYS_SYSINFO/SYS_PROC_LIST
   e pelos apps de usuario (fetch, mem, ps). */

int sysinfo_get(system_info_t *out){
    if(!out) return FS_ERROR;
    uint64_t total = pmm_total_mem();
    uint64_t free  = pmm_free_mem();
    out->memory_total = total;
    out->memory_free  = free;
    out->memory_used  = (total > free) ? (total - free) : 0;
    out->cpu_count    = 1;                 /* x86_64; SMP nao iniciado */
    out->uptime_ms    = ticks * 10;        /* PIT 100Hz -> ms */
    out->ticks        = ticks;

    int n = 0;
    for(int i=0;i<MAX_PROCS;i++){
        process_t *p = proc_at(i);
        if(p && p->present) n++;
    }
    out->process_count = n;
    return FS_OK;
}

int sysinfo_proc_list(proc_info_t *out, int max){
    if(!out || max<=0) return 0;
    int n=0;
    for(int i=0;i<MAX_PROCS && n<max;i++){
        process_t *p = proc_at(i);
        if(!p || !p->present) continue;
        out[n].pid = p->pid;
        strncpy(out[n].name, p->name, SYSINFO_PROC_NAME_MAX-1);
        out[n].name[SYSINFO_PROC_NAME_MAX-1]=0;
        out[n].state  = p->state;
        out[n].is_user= (uint8_t)(p->is_user?1:0);
        out[n].exit_code = p->exit_code;
        n++;
    }
    return n;
}

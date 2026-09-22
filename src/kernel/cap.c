#include "thais.h"
#include <stdbool.h>

/* cap.c — Sistema real de capabilities (Phase C).
   As capabilities estao armazenadas em process_t.rights (bitmask).
   cap_check verifica; cap_grant/revoke modifica per-process. */

void cap_init(void){
    /* process_t ja inicializado por proc_init */
    kprint("[cap] sistema de capabilities pronto\n");
}

bool cap_check(uint32_t pid, uint64_t right){
    process_t *p=proc_get(pid);
    if(!p) return false;
    return (p->rights & right) != 0;
}

bool cap_grant(uint32_t pid, uint64_t rights){
    process_t *p=proc_get(pid);
    if(!p) return false;
    p->rights |= rights;
    return true;
}

bool cap_revoke(uint32_t pid, uint64_t rights){
    process_t *p=proc_get(pid);
    if(!p) return false;
    p->rights &= ~rights;
    return true;
}

bool cap_has(uint32_t pid, uint64_t right){
    return cap_check(pid, right);
}

void kinesis_init(void){ cap_init(); }

/* wrappers de kinesis sobre a tabela real de processos.
   PLACEHOLDER (Fase M): sao apenas REGISTRO de processos com caps — o
   processo e criado mas permanece em PROC_CREATED (nunca entra na ready
   queue) porque nao ha entry point real. Execucao real usa proc_create/
   proc_create_user com entry valido. */
static uint32_t kinesis_register_only(const char *name, uint64_t caps){
    if(!name || !*name) return 0;
    process_t p;
    int pid=proc_create(name, caps, 0, &p);
    if(pid<=0) return 0;
    /* cria como "registrado", fora da escala (peda do scheduler) */
    process_t *rp=proc_get((uint32_t)pid);
    if(rp) rp->state=PROC_CREATED;
    return (uint32_t)pid;
}

uint32_t kinesis_spawn(const char *name, uint64_t caps){
    return kinesis_register_only(name, caps);
}

uint32_t kinesis_register(const char *name, uint64_t caps){ return kinesis_register_only(name, caps); }

void kinesis_list(void){
    kprint("[kinesis] processos:\n");
    char buf[96];
    static const char *st[]={"CRIADO","PRONTO","RODANDO","BLOQUEADO","TERMINADO"};
    for(int i=0;i<MAX_PROCS;i++){
        process_t *p=proc_at(i);
        if(!p || !p->present || p->state==PROC_TERMINATED) continue;
        int s=p->state; if(s<0||s>4) s=0;
        snprintf(buf,96,"  pid %u %-14s [%s] rights=%llx\n",
            p->pid, p->name, st[s], (unsigned long long)p->rights);
        kprint(buf);
    }
}

process_t* kinesis_get(uint32_t pid){ return proc_get(pid); }

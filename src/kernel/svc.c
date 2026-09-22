#include "thais.h"
#include <stdbool.h>

/* svc.c — Service Registry (MARCO 9): registro de servicos userspace por
   nome. Permite que processos se registrem como servicos IPC e que outros
   os encontrem pelo nome. Fundamento para fsd, devd e futuros servicos. */

#define SVC_MAX 16

typedef struct {
    char     name[32];
    uint32_t pid;
    bool     active;
} svc_entry_t;

static svc_entry_t svc_table[SVC_MAX];
static int svc_count = 0;

void svc_init(void){
    for(int i=0;i<SVC_MAX;i++){
        svc_table[i].name[0]=0;
        svc_table[i].pid=0;
        svc_table[i].active=false;
    }
    svc_count=0;
    kprint("[svc] service registry pronto\n");
}

int svc_register(const char *name){
    if(!name || !*name || !current_proc) return -1;
    /* ja registrado? */
    for(int i=0;i<SVC_MAX;i++){
        if(svc_table[i].active && strcmp(svc_table[i].name, name)==0){
            svc_table[i].pid = current_proc->pid;
            return 0;  /* atualiza */
        }
    }
    /* novo */
    for(int i=0;i<SVC_MAX;i++){
        if(!svc_table[i].active){
            strncpy(svc_table[i].name, name, 31);
            svc_table[i].name[31]=0;
            svc_table[i].pid = current_proc->pid;
            svc_table[i].active = true;
            svc_count++;
            char b[80]; snprintf(b,80,"[svc] registrado: '%s' pid=%u\n", name, current_proc->pid);
            kprint(b);
            return 0;
        }
    }
    return -2;  /* tabela cheia */
}

int svc_query(const char *name, uint32_t *out_pid){
    if(!name || !*name) return -1;
    for(int i=0;i<SVC_MAX;i++){
        if(svc_table[i].active && strcmp(svc_table[i].name, name)==0){
            /* confere se o processo ainda existe */
            process_t *p = proc_get(svc_table[i].pid);
            if(p && p->present && p->state!=PROC_TERMINATED){
                if(out_pid) *out_pid = svc_table[i].pid;
                return 0;
            }
            /* processo morreu: limpa */
            svc_table[i].active = false;
            svc_count--;
        }
    }
    return -2;
}
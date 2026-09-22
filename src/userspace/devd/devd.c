/* devd — device daemon (MARCO 9). Servico userspace de dispositivos/sistema.
   Registra o nome "devd" e atende requisicoes IPC: UPTIME e SYSINFO. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

/* ferramenta de impressao decimal num buffer (sem libc) */
static void spud_print_dec_in(uint64_t v, char *b, size_t max){
    char t[24]; int i=(int)sizeof(t); t[--i]=0;
    if(v==0) t[--i]='0';
    while(v){ t[--i]=(char)('0'+(v%10)); v/=10; }
    size_t n=spud_len(&t[i]);
    if(n>=max) n=max-1;
    for(size_t k=0;k<n;k++) b[k]=t[i+k];
    b[n]=0;
}

static int devd_handler(uint32_t from, uint32_t type,
                        const char *req, char *resp, size_t resp_max){
    (void)from; (void)req;
    if(type==IPC_TYPE_PING){
        spud_ncpy(resp, "pong", resp_max);
        return 4;
    }
    if(type==IPC_TYPE_UPTIME){
        long ms=spud_sys(SYS_UPTIME, 0, 0, 0, 0, 0);
        char tmp[40];
        spud_ncpy(tmp, "uptime=", sizeof(tmp));
        spud_print_dec_in((uint64_t)(ms>0?ms:0), tmp+7, sizeof(tmp)-7);
        spud_ncpy(resp, tmp, resp_max);
        return (int)spud_len(tmp);
    }
    if(type==IPC_TYPE_SYSINFO){
        spud_sysinfo_t si;
        long r=spud_sys(SYS_SYSINFO, (uint64_t)(uintptr_t)&si, 0, 0, 0, 0);
        char tmp[256];
        if(r!=0){
            spud_ncpy(tmp, "sysinfo: erro", sizeof(tmp));
        } else {
            spud_ncpy(tmp, "mem=", sizeof(tmp));
            spud_print_dec_in(si.memory_total, tmp+4, sizeof(tmp)-4);
            spud_ncat(tmp, " proc=", sizeof(tmp));
            spud_print_dec_in(si.process_count, tmp+spud_len(tmp), sizeof(tmp)-spud_len(tmp));
        }
        spud_ncpy(resp, tmp, resp_max);
        return (int)spud_len(tmp);
    }
    spud_ncpy(resp, "?", resp_max);
    return 1;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    spud_svc_serve("devd", devd_handler);
    /* so sai daqui se o registro falhou */
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 0;
}
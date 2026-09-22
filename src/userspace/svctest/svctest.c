/* svctest — cliente de servicos (MARCO 9). Consulta os servicos devd/fsd via
   SYS_SVC_QUERY + IPC_SEND/RECV e verifica as respostas. Deterministico. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

static void dbg_num(long v){
    char t[16]; int n=0;
    if(v<0){ spud_write_ch('-'); v=-v; }
    if(v==0){ spud_write_ch('0'); return; }
    while(v>0 && n<(int)sizeof(t)-1){ t[n++]=(char)('0'+v%10); v/=10; }
    while(n--) spud_write_ch(t[n]);
}

static int query_and_ping(const char *name){
    long pid=spud_svc_query_retry(name);
    if(pid<=0){ spud_write("[svctest] erro: nao achou servico "); spud_write(name); spud_write("\n"); return 1; }
    spud_write("  query "); spud_write(name); spud_write(" -> pid="); dbg_num(pid); spud_write("\n");
    char buf[256];
    long r=spud_ipc_send((uint32_t)pid, IPC_TYPE_PING, 0, 0);
    if(r!=0){ spud_write("  ping send falhou\n"); return 2; }
    uint32_t from=0,type=0;
    long got=spud_ipc_recv_retry(&from,&type,buf,sizeof(buf));
    if(got<0 || type!=IPC_TYPE_PING || from!=(uint32_t)pid){
        spud_write("  ping recv invalido (got="); dbg_num(got);
        spud_write(" from="); dbg_num((long)from);
        spud_write(" type="); dbg_num((long)type);
        spud_write(")\n"); return 3;
    }
    buf[got]=0;
    spud_write("  "); spud_write(name); spud_write(" ping -> ");
    spud_write(buf); spud_write("\n");
    return 0;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    spud_write("[svctest] testando servicos\n");
    int fails=0;

    fails += query_and_ping("devd");

    long devd=spud_svc_query_retry("devd");
    if(devd>0){
        char buf[256];
        long r=spud_ipc_send((uint32_t)devd, IPC_TYPE_UPTIME, 0, 0);
        uint32_t from=0,type=0;
        long got=r==0?spud_ipc_recv_retry(&from,&type,buf,sizeof(buf)):-1;
        if(got>=0){ buf[got]=0; spud_write("  devd uptime -> "); spud_write(buf); spud_write("\n"); }
        else fails++;
    } else fails++;

    fails += query_and_ping("fsd");

    long fsd=spud_svc_query_retry("fsd");
    if(fsd>0){
        char buf[1024];
        const char *path="/nomos/manifesto.txt";
        long r=spud_ipc_send((uint32_t)fsd, IPC_TYPE_READ, path, spud_len(path));
        uint32_t from=0,type=0;
        long got=r==0?spud_ipc_recv_retry(&from,&type,buf,sizeof(buf)-1):-1;
        if(got>0){
            buf[got]=0;
            spud_write("  fsd read /nomos/manifesto.txt -> ");
            spud_write(buf); spud_write("\n");
        } else fails++;
    } else fails++;

    /* ABI 1.5: superfície completa do fsd — WRITE/CREATE/REMOVE/AUDIT via
       servico (com fallback syscall). Ciclo create->write->read->remove e
       trilho de auditoria do servico (sem fallback). */
    {
        const char *t="/paradosis/svctest-fsd.txt";
        const char *data="ola fsd ring 3";
        long fails2=0;
        spud_fsd_remove(t);
        if(spud_fsd_create(t)!=1) fails2++;
        if(spud_fsd_write(t, data, spud_len(data))!=1) fails2++;
        char rb[128]; long rr=spud_fsd_read(t, rb, sizeof(rb));
        if(rr<0 || spud_len(data)!=(size_t)rr ||
           spud_memcmp(rb, data, (size_t)rr)!=0) fails2++;
        char lb[1024];
        if(spud_fsd_list("/paradosis", lb, sizeof(lb))<0 ||
           spud_strstr(lb, "svctest-fsd.txt")==0) fails2++;
        char ab[1024];
        if(spud_fsd_audit(ab, sizeof(ab))<0 ||
           spud_strstr(ab, t)==0) fails2++;
        if(spud_fsd_remove(t)!=1) fails2++;
        if(fails2==0){
            spud_write("  fsd write/create/read/remove/audit via IPC -> ok\n");
            spud_write("[svctest] fsd IPC ops ok\n");
        } else fails++;
    }

    /* ABI 1.5: servico 'auth' (authd, ring 3) — dono das credenciais.
       Valida RPC (nao muta estado): count responde e login de conta
       inexistente deve responder "0". Fracas crivam o marco 9. */
    fails += query_and_ping("auth");

    long auth=spud_svc_query_retry("auth");
    if(auth>0){
        char out[64];
        long got=spud_auth_rpc(IPC_TYPE_AUTH_COUNT, 0, 0, out, sizeof(out));
        if(got>=0){
            spud_write("  auth count -> "); spud_write(out); spud_write("\n");
        } else fails++;

        char req[48];
        const char *u="zzz-inexistente", *p="teste123";
        size_t n=0; size_t k;
        for(k=0;u[k] && n+1<sizeof(req);k++) req[n++]=u[k]; req[n++]=0;
        for(k=0;p[k] && n+1<sizeof(req);k++) req[n++]=p[k]; req[n++]=0;
        long got2=spud_auth_rpc(IPC_TYPE_AUTH_LOGIN, req, n, out, sizeof(out));
        if(got2==1 && out[0]=='0'){
            spud_write("  auth verify-inexistente -> 0 (ok)\n");
            spud_write("[svctest] auth IPC ok\n");
        } else fails++;
    } else fails++;

    /* ABI 1.6: servico 'synd' (ring 3) — dono da politica de contratos
       (store userspace semeado do kernel). Valida RPC: ping responde e a
       listagem/leitura trazem o contrato de /synallagma (idios.pacto →
       /idios). Se o seed falhou, read(/idios) devolve "0" e criva. */
    fails += query_and_ping("synd");

    long synd=spud_svc_query_retry("synd");
    if(synd>0){
        char out[512];
        long got=spud_synd_list("", out, sizeof(out));
        long got2=spud_synd_read("/idios", out, sizeof(out));
        if(got>0 && got2>1 && out[0]!='0'){
            spud_write("  synd list+read /idios -> ");
            spud_write(out);
            spud_write("[svctest] synd IPC ok\n");
        } else {
            fails++;
            spud_write("  synd IPC falhou (list="); dbg_num(got);
            spud_write(" read="); dbg_num(got2); spud_write(")\n");
        }
    } else fails++;

    if(fails==0){
        spud_write("[svctest] MARCO 9 OK (devd+fsd+auth respondem via IPC)\n");
        spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
        return 0;
    }
    spud_write("[svctest] FALHAS detectadas\n");
    spud_sys(SYS_EXIT, 1, 0, 0, 0, 0);
    return 1;
}
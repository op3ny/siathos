#include "thais.h"
#include <stdbool.h>

/* ipc.c — IPC com message queues (Phase H).
   Cada processo tem uma mailbox. Envio copia a mensagem para a mailbox do
   destino e acorda o processo (se bloqueado). Receber bloqueia se vazio.
   A capacidade CAP_IPC e exigida para trocar mensagens. */
#define MAILBOX_MAX 64
typedef struct {
    uint32_t pid;
    ipc_message_t msgs[IPC_QUEUE_MAX];
    int head;
    int count;
} mailbox_t;

static mailbox_t mailboxes[MAILBOX_MAX];
static bool ipc_ready=false;

void ipc_init(void){
    for(int i=0;i<MAILBOX_MAX;i++){ mailboxes[i].pid=0; mailboxes[i].head=0; mailboxes[i].count=0; }
    ipc_ready=true;
}

static mailbox_t* mailbox_for(uint32_t pid){
    if(!ipc_ready) return 0;
    for(int i=0;i<MAILBOX_MAX;i++) if(mailboxes[i].pid==pid) return &mailboxes[i];
    /* cria mailbox sob demanda */
    for(int i=0;i<MAILBOX_MAX;i++) if(mailboxes[i].pid==0){ mailboxes[i].pid=pid; mailboxes[i].head=0; mailboxes[i].count=0; return &mailboxes[i]; }
    return 0;
}

static void mailbox_push(mailbox_t *mb, const ipc_message_t *m){
    if(mb->count>=IPC_QUEUE_MAX) return;
    int idx=(mb->head + mb->count) % IPC_QUEUE_MAX;
    mb->msgs[idx]=*m;
    mb->count++;
}

static int mailbox_pop(mailbox_t *mb, ipc_message_t *out){
    if(mb->count<=0) return -1;
    *out=mb->msgs[mb->head];
    mb->head=(mb->head+1)%IPC_QUEUE_MAX;
    mb->count--;
    return 0;
}

static bool ipc_cap_ok(void){
    if(!current_proc) return false;
    if(current_proc->rights & CAP_IPC) return true;
    return false;
}

static int do_send(uint32_t to, uint32_t type, const void *data, size_t size){
    if(!ipc_cap_ok()) return -1;
    mailbox_t *mb=mailbox_for(to);
    if(!mb) return -2;
    ipc_message_t m; memset(&m,0,sizeof(m));
    m.sender = current_proc->pid;
    m.receiver = to;
    m.type = type;
    if(size>IPC_MSG_MAX) size=IPC_MSG_MAX;
    if(size) memcpy(m.payload,data,size);
    m.size=size;
    mailbox_push(mb,&m);
    /* acorda o destino se estava bloqueado esperando mensagem */
    sched_wakeup(to);
    return 0;
}

int ipc_send(uint32_t to, uint32_t type, const void *data, size_t size){
    return do_send(to,type,data,size);
}

/* RPC request/reply: envia a mensagem ao destino e BLOQUEIA ate que uma
   resposta chegue a este processo (chamada de ipc_reply / ipc_send do
   receptor). Base para o modelo Synallagma (contrato -> IPC -> servico). */
int ipc_send_blocking(uint32_t to, uint32_t type, const void *data, size_t size){
    int r=do_send(to,type,data,size);
    if(r!=0) return r;
    uint32_t from=0, rt=0; size_t got=0;
    uint8_t tmp[IPC_MSG_MAX];
    if(ipc_receive(&from,&rt,tmp,sizeof(tmp),&got)!=0) return -3;
    return 0;
}

int ipc_receive(uint32_t *from, uint32_t *type, void *buf, size_t max, size_t *out_size){
    if(!ipc_cap_ok()) return -1;
    uint32_t me = current_proc->pid;
    mailbox_t *mb=mailbox_for(me);
    if(!mb) return -2;
    while(mb->count<=0){
        sched_block();          /* bloqueia; sera acordado por um ipc_send */
        mb=mailbox_for(me);
        if(!mb) return -2;
    }
    ipc_message_t m;
    if(mailbox_pop(mb,&m)!=0) return -2;
    if(from) *from=m.sender;
    if(type) *type=m.type;
    size_t to=m.size; if(to>max) to=max;
    if(to) memcpy(buf,m.payload,to);
    if(out_size) *out_size=to;
    return 0;
}

/* Variante NAO-bloqueante: pega a mensagem ja na mailbox ou devolve -3 sem
   bloquear em sched_block. Usado em SYS_READ (polling do teclado) para evitar
   o park profundo da cadeia C dentro de syscall — o poller re-tenta na proxima
   iteracao (spud_getc faz 16 reads por yield). */
int ipc_try_receive(uint32_t *from, uint32_t *type, void *buf, size_t max, size_t *out_size){
    if(!ipc_cap_ok()) return -1;
    uint32_t me = current_proc->pid;
    mailbox_t *mb=mailbox_for(me);
    if(!mb) return -2;
    if(mb->count<=0) return -3;
    ipc_message_t m;
    if(mailbox_pop(mb,&m)!=0) return -2;
    if(from) *from=m.sender;
    if(type) *type=m.type;
    size_t to=m.size; if(to>max) to=max;
    if(to) memcpy(buf,m.payload,to);
    if(out_size) *out_size=to;
    return 0;
}

int ipc_reply(uint32_t to, uint32_t type, const void *data, size_t size){
    return do_send(to,type,data,size);
}

#include "thais.h"
#include "fetch_img.h"
#include "mem_img.h"
#include "echo_img.h"
#include "ps_img.h"
#include "ls_img.h"
#include "cat_img.h"
#include <stdbool.h>

/* apps.c — infraestrutura de apps de usuario embutidos em RING 3 (Fase K/ABI 1.0).
   Cada app e um binario flat PIC (nasm -f bin, ver scripts/genapp.py) embutido
   no kernel. Ao executar, spawnamos um processo ring 3 via proc_create_user_args
   (espaco de enderecos proprio, W^X, iretq) que pode receber argc/argv (montados
   na stack de usuario e entregues como rdi/rsi ao _start). Diferente dos ELF
   ring0 do executor classico (exec_elf), estes rodam de verdade em anel 3.
   Resolvemos por nome de caminho para permitir: praxia /praxis/<app>. */

#define APPS_USER_VADDR 0x200000000000u
#define APPS_MAX 16

typedef struct {
    const char *name;            /* nome base (ex: "fetch") */
    const unsigned char *img;
    size_t size;
} app_t;

static const app_t app_table[] = {
    { "fetch", fetch_img, FETCH_IMG_SIZE },
    { "mem",   mem_img,   MEM_IMG_SIZE   },
    { "echo",  echo_img,  ECHO_IMG_SIZE  },
    { "ps",    ps_img,    PS_IMG_SIZE    },
    { "ls",    ls_img,    LS_IMG_SIZE    },
    { "cat",   cat_img,   CAT_IMG_SIZE   },
};

#define APP_COUNT (sizeof(app_table)/sizeof(app_table[0]))

static int apps_spawn_idx(int idx, int argc, const char **argv){
    const app_t *a = &app_table[idx];
    process_t r3;
    int pid = proc_create_user_args(a->name, CAP_SYSCALL|CAP_FS_READ,
                                    APPS_USER_VADDR,
                                    (void*)a->img, a->size,
                                    argc, argv,
                                    &r3);
    if(pid < 0){
        kprint("[apps] falha ao criar processo ring 3\n");
        return pid;
    }
    char b[80]; snprintf(b,80,"[apps] '%s' rodando em ring 3 (pid %d)\n", a->name, pid);
    kprint(b);
    return pid;
}

/* Procura um app embutido pelo nome normalizado (sem diretorio/extensao).
   Retorna indice tabela ou -1. */
static int apps_lookup_name(const char *name){
    if(!name || !name[0]) return -1;
    for(size_t i=0;i<APP_COUNT;i++)
        if(strcmp(app_table[i].name, name)==0) return (int)i;
    return -1;
}

/* Tenta executar um app embutido a partir de um caminho resolvido de shell.
   path: caminho completo ja normalizado (ex: "/praxis/fetch" ou "/bin/fetch").
   argc/argv: argumentos repassados ao _start do app (opcionais, nulos em 0).
   Retorna 1 se executou (app embutido), 0 se nao, negativo em erro. */
int apps_try_run_args(const char *path, int argc, const char **argv, uint32_t *out_pid){
    if(path && out_pid) *out_pid=0;
    if(!path || !path[0]) return 0;
    /* usa o ultimo componente do caminho como nome do app */
    const char *base = strrchr(path,'/');
    base = base ? base+1 : path;
    int idx = apps_lookup_name(base);
    if(idx<0) return 0;
    int pid = apps_spawn_idx(idx, argc, argv);
    if(out_pid) *out_pid = (uint32_t)pid;
    return pid>=0 ? 1 : pid;
}

int apps_try_run(const char *path, uint32_t *out_pid){
    return apps_try_run_args(path, 0, NULL, out_pid);
}

int apps_spawn_fetch(void){
    return apps_spawn_idx(0, 0, NULL);   /* 'fetch' e o indice 0 da tabela */
}

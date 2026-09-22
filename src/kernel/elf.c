#include "thais.h"
#include <stdbool.h>

/* elf.c — ELF64 loader real (Phase O + MARCO 1: ring 3).
   Valida o header, e para cada segmento PT_LOAD mapeia paginas de USUARIO no
   espaco do processo (vaddr do próprio ELF, area de usuario >= USER_VADDR_BASE),
   W^X por flags do segmento, zerando o bss. O processo roda em ANEL 3 com
   argc/argv montados na stack de usuario (proc_create_user_regions). */

static uint64_t u64(const uint8_t*p){ uint64_t v=0; for(int i=0;i<8;i++) v|=((uint64_t)p[i])<<(i*8); return v; }
static uint32_t u32(const uint8_t*p){ uint32_t v=0; for(int i=0;i<4;i++) v|=((uint32_t)p[i])<<(i*8); return v; }
static uint16_t u16(const uint8_t*p){ uint16_t v=0; for(int i=0;i<2;i++) v|=((uint16_t)p[i])<<(i*8); return v; }

int exec_elf(const char *path, uint32_t *out_pid){
    return exec_elf_args(path, 0, 0, out_pid);
}

/* Nucleo do ELF loader (ABI 1.5): recebe o nome do processo e o buffer ELF
   (de RAMFS via kmalloc, ou dos bytes embutidos de s_mods[], que sao estaveis
   em .rodata). `owned`=1 libera o buffer ao final; =0 nao (modulo embutido). */
static int exec_elf_core(const char *pname, const unsigned char *img, size_t n,
                         int argc, const char **argv, uint32_t *out_pid, int owned){
    size_t got=n;
    /* validar ELF64 */
    if(!img || got<64){ if(owned) kfree((void*)img); return -1; }
    if(img[0]!=0x7F||img[1]!='E'||img[2]!='L'||img[3]!='F'){
        if(owned) kfree((void*)img); return -1;
    }
    if(img[4]!=2){ kprint("exec_elf: nao 64-bit\n"); if(owned) kfree((void*)img); return -1; }
    if(img[5]!=1){ kprint("exec_elf: endian errado\n"); if(owned) kfree((void*)img); return -1; }
    uint16_t machine = u16(img+18);
    uint64_t entry   = u64(img+24);
    uint64_t phoff   = u64(img+32);
    uint16_t phentsz = u16(img+54);
    uint16_t phnum   = u16(img+56);
    if(machine!=0x3E){ kprint("exec_elf: nao x86_64\n"); if(owned) kfree((void*)img); return -1; }
    if(phnum==0){ kprint("exec_elf: sem program headers\n"); if(owned) kfree((void*)img); return -1; }
    if(phentsz < 56){ kprint("exec_elf: phentsz invalido\n"); if(owned) kfree((void*)img); return -1; }
    if(phoff + (uint64_t)phnum*phentsz > got){ kprint("exec_elf: ph tabela fora do arquivo\n"); if(owned) kfree((void*)img); return -1; }

    /* montar regioes PT_LOAD (user space, vaddr do ELF; max 7 segmentos) */
    ureg_t regs[7];
    int nregs=0;
    for(uint16_t i=0;i<phnum;i++){
        const uint8_t *ph=img+phoff + (size_t)i*phentsz;
        if(u32(ph)!=1) continue;              /* apenas PT_LOAD */
        uint64_t poff   = u64(ph+8);
        uint64_t pvaddr = u64(ph+16);
        uint64_t pfilesz= u64(ph+32);
        uint64_t pmemsz = u64(ph+40);
        if((uint64_t)poff+pfilesz > got){ if(owned) kfree((void*)img); return -1; }
        /* nunca mapeie em espaco do kernel; proc_create_user_regions exige
           area de USUARIO (>= USER_VADDR_BASE) p/ nao colidir com a identidade */
        if(pvaddr >= 0xffff800000000000ULL){ kprint("exec_elf: vaddr no espaco do kernel\n"); if(owned) kfree((void*)img); return -1; }
        regs[nregs].vaddr = pvaddr;
        regs[nregs].src   = img+poff;
        regs[nregs].fileSz= pfilesz;
        regs[nregs].memSz = pmemsz;
        regs[nregs].flags = u32(ph+4);        /* PF_X/PF_W (W^X por segmento) */
        nregs++;
        if(nregs>=7) break;
    }
    if(nregs==0){ kprint("exec_elf: sem segmentos carregaveis\n"); if(owned) kfree((void*)img); return -1; }

    /* cria processo ring 3 (mapa proprio, W^X por seg, argc/argv na stack).
       pname ja chega como nome simples (basename) nos dois caminhos. */
    process_t r3;
    /* Cria processo ring 3. Usuarios HERDAM as capabilities do processo pai
       (que por sua vez herdou as da conta no SYS_SESSION_LOGIN). Isso preserva
       admin/soberano: praxia rodeia com CAP_ALL e passa adiante aos apps. */
    uint64_t rights = CAP_SYSCALL|CAP_FS_READ|CAP_FS_WRITE|CAP_EXEC|CAP_IPC;
    if(login_result_user() && current_proc) rights |= current_proc->rights;
    int pid=proc_create_user_regions(pname, rights,
        entry, regs, nregs, argc, argv, &r3);
    if(owned) kfree((void*)img);
    if(pid<=0){ kprint("exec_elf: falha ao criar processo\n"); return -1; }
    char b[80]; snprintf(b,80,"exec_elf: '%s' em anel 3 (pid %d, %d segmentos)\n",
        pname, pid, nregs); kprint(b);
    if(out_pid) *out_pid=(uint32_t)pid;
    return FS_OK;
}

int exec_elf_args(const char *path, int argc, const char **argv, uint32_t *out_pid){
    /* Carrega o ELF inteiro na heap (nao num buffer estatico de 1MB: isso
       deixava 1MB de .bss no kernel e nao era reentrante). Libera ao final. */
    if(!path || !*path) return -1;
    uint8_t *img;
    size_t got=0;
    img=kmalloc(1<<20);
    if(!img){ kprint("exec_elf: sem memoria\n"); return -1; }
    if(fs_read(path, img, 1<<20, &got)!=FS_OK || got<64){
        kprint("exec_elf: falha ao ler\n"); kfree(img); return -1;
    }
    char pname[PROC_NAME_MAX];
    const char *slash=strrchr(path,'/');
    strncpy(pname, slash?slash+1:path, PROC_NAME_MAX-1); pname[PROC_NAME_MAX-1]=0;
    return exec_elf_core(pname, img, got, argc, argv, out_pid, 1);
}

/* exec_embedded_args — modulo userspace direto dos bytes embutidos (s_mods[]),
   sem fs_read. Usado pelo boot (kormi, sessao) e como caminho primario do
   exec de apps do sistema — desacopla exec do ramfs (ABI 1.5, enabler). */
int exec_embedded_args(const char *name, int argc, const char **argv, uint32_t *out_pid){
    if(!name || !*name) return -1;
    size_t n=fs_mod_count();
    for(size_t i=0;i<n;i++){
        if(strcmp(name, fs_mod_name(i))==0){
            size_t esz=0;
            const unsigned char *elf=fs_mod_elf(i,&esz);
            if(!elf || esz<64) return -1;
            char pname[PROC_NAME_MAX];
            strncpy(pname, name, PROC_NAME_MAX-1); pname[PROC_NAME_MAX-1]=0;
            return exec_elf_core(pname, elf, esz, argc, argv, out_pid, 0);
        }
    }
    return -1;
}

/* exec_user_path — ponto unico de execucao ELF ring 3 com args, usado por
   SYS_EXEC (syscall) e por hooks de teste (TEST=1). ABI 1.5: paths do sistema
   (/bin, /kormi, /praxis) executam da TABELA EMBUTIDA (exec_embedded_args),
   sem depender do ramfs; qualquer outro path (scripts/arquivos do usuario)
   valida existencia/permissao via VFS e le do FS (fallback classico). */
int exec_user_path(const char *path, int argc, const char *const *argv, uint32_t *out_pid){
    if(!path || !*path) return -1;
    static const char *sysdirs[]={ "/bin/", "/kormi/", "/praxis/" };
    for(size_t k=0;k<sizeof(sysdirs)/sizeof(sysdirs[0]);k++){
        size_t dl=strlen(sysdirs[k]);
        if(strncmp(path, sysdirs[k], dl)==0 && path[dl] && !strchr(path+dl,'/')){
            int r=exec_embedded_args(path+dl, argc, argv, out_pid);
            if(r==FS_OK) return r;
            /* nao e modulo embutido (ex.: arquivo do usuario) -> trata via FS */
            break;
        }
    }
    if(!fs_exists(path)){
        char b[128]; snprintf(b,128,"exec_user_path: nao encontrado '%s'\n",path); kprint(b);
        return -1;
    }
    if(!vfs_check_cap(path, CAP_FS_READ)){
        char b[128]; snprintf(b,128,"exec_user_path: sem permissao '%s'\n",path); kprint(b);
        return -1;
    }
    return exec_elf_args(path, argc, argv, out_pid);
}

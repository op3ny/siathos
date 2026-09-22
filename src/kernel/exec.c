#include "thais.h"
#include <stdbool.h>

/* exec.c — execucao de arquivos e editor de texto do sistema.
   exec_file() detecta ELF e delega ao exec_elf() real (Phase O); senao
   interpreta como script minimo. */

void edit_file(const char *path){
    char full[FS_NAME_MAX];
    strncpy(full, path, FS_NAME_MAX);
    full[FS_NAME_MAX-1]=0;

    char buf[2048]; size_t got=0;
    if(fs_read(full, buf, sizeof(buf)-1, &got)==FS_OK){
        buf[got]=0;
        fb_console_write("--- conteudo atual ---\n");
        fb_console_write(buf);
        if(got>0 && buf[got-1]!='\n') fb_console_write("\n");
        fb_console_write("--- fim ---\n");
    } else {
        fb_console_write("arquivo novo\n");
        if(fs_create(full,FS_TYPE_FILE)!=FS_OK){ fb_console_write("falha ao criar\n"); return; }
        got=0; buf[0]=0;
    }
    fb_console_write("Digite linhas, linha vazia ou \".\" sozinho encerra.\n");
    char line[256];
    char out[2048]; size_t out_len=0; out[0]=0;
    while(out_len < sizeof(out)-256){
        fb_console_write("> "); serial_write("> ");
        keyboard_get_line(line,sizeof(line),true,false);
        if(strcmp(line,".")==0 || strlen(line)==0) break;
        size_t l=strlen(line);
        if(out_len + l + 1 >= sizeof(out)){ fb_console_write("arquivo muito grande\n"); break; }
        memcpy(out+out_len, line, l); out_len+=l; out[out_len++]='\n'; out[out_len]=0;
    }
    if(out_len>0){
        if(fs_write(full, out, out_len)==FS_OK) fb_console_write("gravado\n");
        else fb_console_write("falha ao gravar\n");
    } else {
        fb_console_write("nada a gravar\n");
    }
}

int exec_file(const char *path){
    return exec_file_args(path, 0, 0);
}

int exec_file_args(const char *path, int argc, const char **argv){
    char full[FS_NAME_MAX]; strncpy(full,path,FS_NAME_MAX); full[FS_NAME_MAX-1]=0;
    uint8_t hdr[4]; size_t got=0;
    if(fs_read(full, hdr, 4, &got)!=FS_OK || got<4){
        fb_console_write("exec: arquivo nao encontrado ou muito pequeno\n"); return -1;
    }
    if(hdr[0]==0x7f && hdr[1]=='E' && hdr[2]=='L' && hdr[3]=='F'){
        fb_console_write("exec: ELF detectado\n");
        uint32_t pid=0;
        int r=exec_elf_args(full, argc, argv, &pid);
        if(r==FS_OK){
            char tmp[64]; snprintf(tmp,64,"  processo %u criado (ELF loader)\n", pid);
            fb_console_write(tmp);
            return 0;
        }
        fb_console_write("  falha ao carregar ELF\n");
        return r;
    }
    /* trata como script/sh - executa linha a linha */
    char script[2048]; size_t rn=0;
    if(fs_read(full, script, sizeof(script)-1, &rn)!=FS_OK){ fb_console_write("exec: falha ao ler\n"); return -1; }
    script[rn]=0;
    fb_console_write("exec: interpretando como script\n");
    char *p=script;
    while(*p){
        char line[256]; int i=0;
        while(*p && *p!='\n' && i<255){ line[i++]=*p++; }
        if(*p=='\n') p++;
        line[i]=0;
        char *s=line; while(*s==' ') s++;
        if(!*s || *s=='#') continue;
        if(strncmp(s,"echo ",5)==0){ fb_console_write(s+5); fb_console_write("\n"); }
        else if(strcmp(s,"clear")==0||strcmp(s,"cls")==0) fb_console_clear();
        else {
            char tmp[280]; snprintf(tmp,280,"  script: %s\n", s);
            fb_console_write(tmp);
        }
    }
    return 0;
}

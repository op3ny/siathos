#include "thais.h"
#include <stdbool.h>

/* synallagma.c — contratos voluntarios que definem permissao de acesso a
   pastas (Phase I). Alem disso, /synallagma e a pasta central de contratos:
   aberta p/ leitura, escrita apenas p/ o soberano (admin).

   Modelo de permissao por contrato (consentimento):
   - Por padrao, TODAS as pastas sao acessiveis (leitura+escrita p/ todos),
     EXCETO /oikos (arquivos do sistema), que e read-only por padrao.
   - Para restringir uma pasta, basta adicionar um contrato na pasta
     (/synallagma/<pasta>.pacto). Entao no login o usuario e consultado.
   - A unica pasta que precisa ser acessivel com escrita p/ pelo menos um
     usuario e /synallagma (o soberano). */

static contract_t contracts[MAX_CONTRACTS];
static int contract_count=0;
static bool consented[MAX_CONTRACTS];       /* consentimiento nesta sessao */
static bool consent_get(int idx);           /* forward decl */

void synallagma_init(void){
    for(int i=0;i<MAX_CONTRACTS;i++){ contracts[i].ativo=false; consented[i]=false; }
    contract_count=0;
    kprint("[synallagma] contratos prontos\n");
}

static void clear_contract(contract_t *c){
    c->nome[0]=0; c->caminho[0]='\0';
    c->leitura=false; c->escrita=false; c->execucao=false;
    c->voluntario=true; c->revogavel=true; c->ativo=false; c->caps_req=CAP_NONE;
}

/* interpreta uma linha 'chave = valor' de um .pacto */
static void parse_line(contract_t *c, const char *line){
    char tmp[FS_NAME_MAX]; size_t i=0;
    const char *p=line;
    while(*p==' '||*p=='\t') p++;
    while(*p && *p!='=' && i<FS_NAME_MAX-1) tmp[i++]=*p++;
    tmp[i]=0;
    /* trim chave */
    while(i>0 && (tmp[i-1]==' '||tmp[i-1]=='\t')) tmp[--i]=0;
    while(*p && *p!='=') { if(*p=='\0') break; p++; }
    if(*p=='=') p++;
    while(*p==' '||*p=='\t') p++;
    /* valor: pode estar entre aspas */
    char val[FS_NAME_MAX]; size_t vi=0;
    if(*p=='\"'){ p++; while(*p && *p!='\"' && vi<FS_NAME_MAX-1) val[vi++]=*p++; }
    else { while(*p && *p!='\n' && *p!='\r' && vi<FS_NAME_MAX-1) val[vi++]=*p++; }
    val[vi]=0;
    if(strcmp(tmp,"caminho")==0||strcmp(tmp,"pasta")==0||strcmp(tmp,"path")==0){
        strncpy(c->caminho,val,FS_NAME_MAX-1);
    } else if(strcmp(tmp,"nome")==0||strcmp(tmp,"titulo")==0){
        strncpy(c->nome,val,64);
    } else if(strcmp(tmp,"leitura")==0||strcmp(tmp,"read")==0){
        c->leitura = (val[0]=='t'||val[0]=='1'||val[0]=='s'||val[0]=='S');
    } else if(strcmp(tmp,"escrita")==0||strcmp(tmp,"write")==0){
        c->escrita = (val[0]=='t'||val[0]=='1'||val[0]=='s'||val[0]=='S');
    } else if(strcmp(tmp,"execucao")==0||strcmp(tmp,"exec")==0){
        c->execucao = (val[0]=='t'||val[0]=='1'||val[0]=='s'||val[0]=='S');
    } else if(strcmp(tmp,"voluntario")==0){
        c->voluntario = (val[0]=='t'||val[0]=='1'||val[0]=='s'||val[0]=='S');
    } else if(strcmp(tmp,"revogavel")==0){
        c->revogavel = (val[0]=='t'||val[0]=='1'||val[0]=='s'||val[0]=='S');
    }
}

int synallagma_scan_contracts(void){
    contract_count=0;
    for(int i=0;i<MAX_CONTRACTS;i++){ clear_contract(&contracts[i]); consented[i]=false; }
    /* varre /synallagma e carrega cada *.pacto */
    char dirbuf[4096]; dirbuf[0]=0;
    if(fs_list("/synallagma", dirbuf, sizeof(dirbuf))!=FS_OK) return 0;
    char *line=dirbuf;
    /* linhas no formato "  <ARQ>   nome" */
    char *nl=line;
    while(nl){
        char *e=strchr(nl,'\n'); if(e)*e=0;
        char *p=nl;
        while(*p==' ') p++;
        if(strncmp(p,"<ARQ>",5)==0){
            p+=5; while(*p==' ') p++;
            /* p = nome do arquivo */
            size_t l=strlen(p);
            if(l>6 && strcmp(p+l-6,".pacto")==0){
                if(contract_count<MAX_CONTRACTS){
                    contract_t *c=&contracts[contract_count++];
                    clear_contract(c);
                    strncpy(c->nome,p,64);
                    /* le arquivo */
                    char path[FS_NAME_MAX]; snprintf(path,FS_NAME_MAX,"/synallagma/%s",p);
                    char content[2048]; size_t got=0;
                    if(fs_read(path,content,sizeof(content)-1,&got)==FS_OK){
                        content[got]=0;
                        char *cl=content;
                        while(cl && *cl){
                            char *ce=strchr(cl,'\n'); if(ce)*ce=0;
                            /* ignora comentarios e secoes [..] */
                            if(cl[0]!='#' && cl[0]!='[' && cl[0]){
                                parse_line(c,cl);
                            }
                            if(ce) cl=ce+1; else cl=0;
                        }
                    }
                    /* se tem contrato sem caminho, usa o nome do arquivo */
                    if(c->caminho[0]==0) strncpy(c->caminho,p,FS_NAME_MAX-1);
                    c->ativo=true;
                }
            }
        }
        if(e) nl=e+1; else nl=0;
    }
    return contract_count;
}

void synallagma_load_contracts(void){
    int n=synallagma_scan_contracts();
    char b[80]; snprintf(b,80,"[synallagma] %d contrato(s) carregado(s)\n", n); kprint(b);
    for(int i=0;i<n;i++){
        char b2[192];
        snprintf(b2,192,"  - '%s' protege '%s' (r=%d w=%d x=%d)\n",
            contracts[i].nome, contracts[i].caminho,
            contracts[i].leitura?1:0, contracts[i].escrita?1:0, contracts[i].execucao?1:0);
        kprint(b2);
    }
}

void synallagma_load_defaults(void){
    /* nada aqui: contratos sao definidos por arquivos em /synallagma */
}

void synallagma_start_all(void){
    /* contratos ficam ativos; consentimento tratado no login */
}

int synallagma_count(void){ return contract_count; }

contract_t* synallagma_contract_at(int idx){
    if(idx<0||idx>=contract_count) return 0;
    return &contracts[idx];
}

void synallagma_list_console(void){
    char b[192];
    for(int i=0;i<contract_count;i++){
        contract_t *c=&contracts[i];
        snprintf(b,sizeof(b),"  %s -> %s (r=%d w=%d x=%d)%s\n",
            c->nome, c->caminho, c->leitura?1:0, c->escrita?1:0, c->execucao?1:0,
            consent_get(i)?" [aceito]":"");
        fb_console_write(b); serial_write(b);
    }
}

contract_t* synallagma_contract_for(const char *path){
    /* retorna o contrato mais especifico que protege 'path' (ou um ancestral) */
    contract_t *best=0; size_t bestlen=0;
    for(int i=0;i<contract_count;i++){
        if(!contracts[i].ativo) continue;
        const char *cp=contracts[i].caminho;
        size_t cl=strlen(cp);
        if(cl==0) continue;
        if(strncmp(path,cp,cl)==0 && (path[cl]=='/' || path[cl]==0 || cp[cl-1]=='/')){
            if(cl>bestlen){ bestlen=cl; best=&contracts[i]; }
        }
    }
    return best;
}

bool synallagma_guards_folder(const char *path){
    /* protege a propria pasta (nao descendentes) - usado p/ exibir o aviso */
    for(int i=0;i<contract_count;i++){
        if(!contracts[i].ativo) continue;
        if(strcmp(contracts[i].caminho,path)==0) return true;
    }
    return false;
}

void consent_grant(int idx){ if(idx>=0&&idx<MAX_CONTRACTS) consented[idx]=true; }
static bool consent_get(int idx){ return idx>=0&&idx<MAX_CONTRACTS?consented[idx]:false; }

/* consulta final: o usuario consentiu este contrato nesta sessao? */
bool synallagma_consented(const char *caminho){
    for(int i=0;i<contract_count;i++) if(contracts[i].ativo && strcmp(contracts[i].caminho,caminho)==0) return consent_get(i);
    return true;   /* sem contrato na pasta => consentimento implicito */
}

/* le um contrato na tela (opcao R). */
static void print_contract(const contract_t *c){
    char b[FS_NAME_MAX+64];
    snprintf(b,sizeof(b),"\n=== Contrato: %s ===\n", c->nome[0]?c->nome:"(sem nome)");
    fb_console_write(b); serial_write(b);
    snprintf(b,sizeof(b),"  Pasta protegida: %s\n", c->caminho);
    fb_console_write(b); serial_write(b);
    snprintf(b,sizeof(b),"  Leitura : %s\n", c->leitura?"permitida":"negada");
    fb_console_write(b); serial_write(b);
    snprintf(b,sizeof(b),"  Escrita : %s\n", c->escrita?"permitida":"negada");
    fb_console_write(b); serial_write(b);
    snprintf(b,sizeof(b),"  Execucao: %s\n", c->execucao?"permitida":"negada");
    fb_console_write(b); serial_write(b);
}

/* Percorre os contratos e apresenta o consentimento.
   S = prosseguir (aceita os contratos) ; N = voltar para o login ;
   R = ler todos os contratos e perguntar de novo.
   Retorna true se todos aceitos, false se o usuario recusou. */
bool consent_prompt_contracts(void){
    if(contract_count==0) return true;
    bool first=true;
    for(int i=0;i<contract_count;i++){
        contract_t *c=&contracts[i];
        if(!c->ativo) continue;
        if(consent_get(i)) continue;              /* ja consentido */
        if(first){ fb_console_write("\nContratos regem o acesso a algumas pastas.\n"); serial_write("\nContratos regem o acesso a algumas pastas.\n"); first=false; }
        char msg[FS_NAME_MAX+64];
        snprintf(msg,sizeof(msg),"Contratos foram identificados em %s.\n", c->caminho);
        fb_console_write(msg); serial_write(msg);
        for(;;){
            fb_console_write("Deseja prosseguir (S/N = voltar para o login) ou ler um dos contratos (R)? ");
            serial_write("Deseja prosseguir (S/N) ou ler contratos (R)? ");
            char ans[8]; keyboard_get_line(ans,sizeof(ans),true,false);
            if(ans[0]=='S'||ans[0]=='s'||ans[0]=='Y'||ans[0]=='y'){
                consent_grant(i);
                fb_console_write("  Contrato aceito. Acesso concedido.\n");
                serial_write("  contrato aceito\n");
                break;
            } else if(ans[0]=='N'||ans[0]=='n'){
                fb_console_write("  Contrato recusado. Voltando ao login.\n"); serial_write("  contrato recusado\n");
                return false;
            } else if(ans[0]=='R'||ans[0]=='r'){
                /* le todos os contratos e pergunta de novo */
                fb_console_write("\n--- Leitura dos contratos ---\n"); serial_write("--- contratos ---\n");
                for(int k=0;k<contract_count;k++) if(contracts[k].ativo) print_contract(&contracts[k]);
                fb_console_write("\n"); /* repete a pergunta do contrato atual */
            }
        }
    }
    return true;
}

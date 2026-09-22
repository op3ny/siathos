/* spoudazo - sessao do usuario em RING 3 (MARCO 2/3).
   Compilado FORA do kernel (gcc -> ELF64), embutido via spoudazo_elf.h e
   executado por init via exec_elf. Faz bootstrap (1o admin) e login
   (verify + consentimento + SESSION_LOGIN). A autenticacao e feita pelo
   SERVICO 'auth' (authd, ring 3, ABI 1.5): o authd verifica e emite um
   ticket de sessao no kernel (SYS_AUTH_TICKET_ISSUE); o kernel consome o
   ticket em SYS_SESSION_LOGIN(a3=tok). Sem authd registrado, cai nos
   syscalls classicos (store do kernel). Apos autenticar, delega a shell
   ao app externo 'praxia' via SYS_EXEC + SYS_WAITPID (MARCO 3). Esse
   comando (e qualquer outro app) abandona a cadeia de spoudazo quando
   encerra. */
#include <stdint.h>
#include <stddef.h>
#include "../spud.h"

static void bootstrap(void){
    spud_write("=== Siaht OS — Primeiro acesso ===\n");
    spud_write("Nenhum usuario encontrado. Crie o administrador.\n");
    char user[32], pass[32], pass2[32];
    for(;;){
        spud_write("Novo admin (nome): "); spud_readline(user, sizeof(user), 1, 0); spud_write("\n");
        if(spud_len(user)<3){ spud_write("nome muito curto.\n"); continue; }
        spud_write("Senha: "); spud_readline(pass, sizeof(pass), 0, 1); spud_write("\n");
        spud_write("Confirme senha: "); spud_readline(pass2, sizeof(pass2), 0, 1); spud_write("\n");
        if(spud_cmp(pass, pass2)!=0){ spud_write("senhas nao conferem.\n"); continue; }
        if(spud_len(pass)<4){ spud_write("senha muito curta (min 4).\n"); continue; }
        long r=spud_auth_create(user, pass);
        if(r==1){ spud_write("Administrador criado. Faca login.\n"); return; }
        spud_write("falha ao criar.\n");
    }
}

static int ask_contracts(void){
    char cbuf[2048];
    long cr=spud_sys(SYS_CONTRACT_LIST, (uint64_t)(uintptr_t)cbuf, sizeof(cbuf), 0, 0, 0);
    if(cr!=0) return 0;
    if(cbuf[0]){
        spud_write("\nContratos regem o acesso a algumas pastas.\n");
        spud_write(cbuf);
        spud_write("Deseja prosseguir (S/N)? ");
        char ans[8]; spud_readline(ans, sizeof(ans), 1, 0);
        spud_write("\n");
        return (ans[0]=='S' || ans[0]=='s' || ans[0]=='Y' || ans[0]=='y') ? 1 : -1;
    }
    return 1;
}

int main(int argc, char **argv){
    (void)argc; (void)argv;
    if(!spud_auth_count()) bootstrap();
    spud_write("=== Siaht OS — Login (contrato voluntario) ===\n");
    spud_write("Made with love by Thais (op3n/op3ny)\n");
    char user[32], pass[32];
    for(;;){
        spud_write("\nusuario: "); spud_readline(user, sizeof(user), 1, 0); spud_write("\n");
        spud_write("senha: "); spud_readline(pass, sizeof(pass), 0, 1); spud_write("\n");
        uint64_t tok=0;
        long ok=spud_auth_login(user, pass, &tok);
        if(ok!=1){ spud_write("senha incorreta. Contrato recusado.\n"); continue; }
        int ca=ask_contracts();
        if(ca!=1){ spud_write("Acesso negado: contrato(s) nao aceito(s).\n"); continue; }
        /* ABI 1.5: a3=ticket emitido pelo authd (authd ausente -> a3=0,
           kernel valida no store classico). */
        long lg=spud_sys(SYS_SESSION_LOGIN, (uint64_t)(uintptr_t)user, 1, tok, 0, 0);
        if(lg!=1){ spud_write("Acesso negado.\n"); continue; }
        spud_write("\nBem-vindo, "); spud_write(user); spud_write("\n");
        /* delega a shell ao app externo 'praxia', passando o usuario.
           "exit" na shell encerra o app com codigo 1 (logout) — o spoudazo
           encerra a sessao e o init (ring 0) reapresenta o login. */
        {
            const char *argv[1]; argv[0]=user;
            long code = spud_exec_wait("/praxis/praxia", 1, argv);
            if(code < 0){
                spud_write("praxia indisponivel (SYS_EXEC falhou).\n");
            } else if(code==0){
                spud_write("sessao encerrada.\n");
            }
        }
        break;
    }
    spud_sys(SYS_EXIT, 0, 0, 0, 0, 0);
    return 0;
}

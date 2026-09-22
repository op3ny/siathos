#include "thais.h"
#include <stdbool.h>

// Login REAL — lê usuario e senha pelo teclado PS/2, valida contra /nomos/accounts
void login_main(thais_fb_t *fb){
    char user[AUTH_NAME_MAX];
    char pass[AUTH_NAME_MAX];
    fb_console_init(fb);
    fb_console_clear();
    // bootstrap: se nenhum usuario, cria admin inicial
    if(auth_user_count()==0){
        fb_console_write("=== Siath OS — Primeiro acesso ===\n");
        fb_console_write("Nenhum usuario encontrado. Crie o administrador.\n");
        serial_write("bootstrap: nenhum usuario\n");
        for(;;){
            fb_console_write("Novo admin (nome): ");
            serial_write("Novo admin: ");
            keyboard_get_line(user,sizeof(user),true,false);
            fb_console_write("\n");
            if(strlen(user)<3){ fb_console_write("nome muito curto.\n"); continue; }
            fb_console_write("Senha: ");
            serial_write("Senha: ");
            keyboard_get_line(pass,sizeof(pass),false,true);
            fb_console_write("\n");
            char pass2[AUTH_NAME_MAX];
            fb_console_write("Confirme senha: ");
            keyboard_get_line(pass2,sizeof(pass2),false,true);
            fb_console_write("\n");
            if(strcmp(pass,pass2)!=0){ fb_console_write("senhas nao conferem.\n"); continue; }
            if(strlen(pass)<4){ fb_console_write("senha muito curta (min 4).\n"); continue; }
            if(auth_create_user(user,pass,CAP_ALL,true)){
                fb_console_write("Administrador criado. Faca login.\n");
                break;
            } else { fb_console_write("falha ao criar.\n"); }
        }
    }
    fb_console_write("=== Siath OS — Login (contrato voluntario) ===\n");
    fb_console_write("Made with love by Thais (op3n/op3ny)\n");
    for(;;){
        fb_console_write("\nusuario: ");
        serial_write("usuario: ");
        keyboard_get_line(user,sizeof(user),true,false);
        serial_write(user); serial_write("\r\n");
        auth_user_t *u=auth_find(user);
        if(!u){ fb_console_write("conta inexistente. Tente criar com bootstrap ou 'auth'.\n"); continue; }
        fb_console_write("senha: ");
        serial_write("senha: ");
        keyboard_get_line(pass,sizeof(pass),false,true);
        serial_write("\r\n");
        if(auth_verify(user,pass)){
            login_set_user(u);
            /* Consentimento voluntario dos contratos que protegem pastas.
               Se o usuario recusar (N), volta ao login (sem sessao). */
            if(!consent_prompt_contracts()){
                login_set_user(0);
                fb_console_write("Acesso negado: contrato(s) nao aceito(s).\n");
                continue;
            }
            char buf[64];
            snprintf(buf,64,"\nBem-vindo, %s. Caps=%llx\n", u->name, (unsigned long long)u->caps);
            fb_console_write(buf);
            serial_write(buf);
            return;
        } else {
            fb_console_write("senha incorreta. Contrato recusado.\n");
            serial_write("senha incorreta.\n");
        }
    }
}

# Siath OS — Serviços: o que são hoje vs. arquitetura-alvo

> Documento das Auditorias (2) e (4): registra **honestamente** o estado atual de
> "Synallagma como serviço" e de "init/login/shell em ring 3", sem tratar plano
> como feito. Atualize junto com `STATUS.md` quando a implementação mudar.

## 1. O modelo atual (monolítico — real)

Hoje `init`, `login` e `shell` **são código residente no kernel** (kernelspace).
Rodam como PID 1 / processos kernel, mas chamam `auth_*`, `fs_*`, `vfs_*`,
`exec_*`, `kinesis_*` e `synallagma_*` **como funções internas** (chamada de
função), não via syscall/IPC:

```
KERNEL: shell + auth + fs + login + exec + synallagma (todos dentro)
```

Consequência prática do que isso **não** é:

- Não há isolamento entre "serviços": um bug no shell corrompe a auth.
- Não há reuso de um serviço por múltiplos consumidores via IPC.
- `Synallagma` **já é serviço** (`synd`, ring 3, ABI 1.6): dono do store
  userspace de contratos — marker `[synd] store ring3` (12º do runtest);
  política em kernel = primitivo/fallback (não é mais o único modo).
- `init` não "inicia serviços"; ele chama o ambiente direto (Login -> Shell).

Isso é um estado **intermediário legítimo** (máquina de decisão real) e é o que
o `README`/`STATUS.md` chamam de "ainda monolítico". Nada aqui é promissemântica:
o roadmap abaixo é *objetivo*, não *feito*.

### 1.1 O que já saiu do kernel: `auth` como serviço ring 3 (2026-09-11, ABI 1.5)

- Novo app `/kormi/authd` (anel 3, PID 8) registra o serviço `"auth"`
  (`spud_svc_serve`) e vira dono da **store de credenciais** e da **política**:
  1º usuário = soberano (CAP_ALL), validação de senha com custo deliberado,
  criação/remoção/troca restritas a `admin_authed`, soberano nunca removido.
- **Sessão por ticket:** `SYS_AUTH_TICKET_ISSUE` (36, exige `CAP_USER_ADMIN`,
  grant do `kormi_start`) grava no kernel `{nome, caps, token usado-uma-vez}`;
  `SYS_SESSION_LOGIN(name, a2=1, a3=tok)` consome o ticket e monta a sessão
  com os caps recebidos do serviço — sem depender do store do kernel.
- **PBKDF2 = primitiva computacional do kernel** (`SYS_AUTH_HASH`=37, IRQs
  off): computo longo em ring 3, preemptado por IRQ, é **não-determinístico**
  aqui (hash corrompia intermitentemente — mesma classe do bug do #PF). O
  authd decide a política e o kernel **aplica** a regra no ponto de enforcement
  (autoridade de regras separada da autoridade de execução — model 3.2 abaixo).
- **Fallback honesto:** se `svc_query("auth")` não achar o serviço, os apps
  caem nos syscalls `SYS_AUTH_*` (store do kernel). Cada boot usa **um** fluxo.
- `spoudazo` (bootstrap/login real), `praxia` (users/userdel/passwd/useradd)
  e `svctest` já consomem o serviço via IPC. Kernel mantém `auth.c` como fallback.

### 1.2 Em preparação para o `fs`: desacoplamento + superfície IPC do `fsd` (2026-09-11)

- **Exec desacoplado do ramfs:** os ELFs embutidos vivem agora na tabela
  global `s_mods[]` (`fs.c`), com accessors `fs_mod_count/name/elf/cfg/is_kormi`.
  O loader ganhou `exec_elf_core` compartilhado e `exec_embedded_args`, e o
  **boot não faz mais `fs_read`**: `kormi_start` e `session_entry` (`spoudazo`)
  executam direto dos bytes embutidos; `exec_user_path` é **embedded-first**
  para `/bin`, `/kormi`, `/praxis` (sem `/` adicional), com fallback em FS para
  scripts/arquivos do usuário. A árvore `/bin,/kormi` continua montada
  (ls/cat/scripts), mas deixou de ser caminho obrigatório do boot.
- **Superfície IPC completa do `fsd`:** além de READ(2)/LIST(3), o serviço
  `"fsd"` atende `IPC_TYPE_FS_CREATE`(11) `path`, `FS_WRITE`(12) `path\n<dados>`,
  `FS_REMOVE`(13) `path` e `FS_AUDIT`(14) — que devolve o **trilho de
  auditoria do próprio serviço** (anéis `audit_op/from/path` em `fsd.c`).
- **Store do serviço (modelo authd):** `fsd` é o **dono do store userspace** —
  `/paradosis` e `/nomos` vivem em **cópia no heap do fsd** (semente via
  `SYS_FS_LIST`/`SYS_FS_READ` no boot, marker `[fsd] store ring3: ... (N
  arquivos)`). Para esses paths, READ/LIST/CREATE/WRITE/REMOVE operam LOCAIS
  (sem syscall); o kernel (SYS_FS_*) é o primitivo/**fallback** nos demais
  paths. Split-brain entre o store do kernel e o do fsd é o modelo assumido
  (um fluxo por boot/consumidor), exatamente como no auth. Helpers
  `spud_fsd_read/list/create/write/remove/audit` no spud.h seguem o padrão
  authd: **svc-first + fallback syscall** (audit não tem fallback — é valor
  só do serviço).
- **Regra IPCA respeitada:** o kernel **não bloqueia** aguardando o fsd (int
  0x80 é interrupt gate = IF 0); os clientes usam IPC não-bloqueante (retry +
  SYS_YIELD) e o fallback syscall cobre a ausência do serviço. O `svctest`
  agora exercita o ciclo create→write→read→remove + auditoria e exige o marker
  `[svctest] fsd IPC ops ok` (validação: 12 markers × 5 boots PASS via
  `_runtest.sh`).
- **Próximo passo (o "de verdade"):** migrar os ~93 usos internos de fs/vfs no
  kernel (shell/init/login/synallagma/exec) para consumir o serviço/fallback,
  com o `fsd` já dono do store userspace. As classes boot/exec/session desta
  sessão já saíram do primitivo — o caminho está aberto (e o store do serviço
  é independente do kernel, pronto para virar a fonte única quando a camada
  interna migrar).

### 1.3 `synd`: Synallagma como serviço ring 3 (2026-09-21, ABI 1.6)

- O app `/kormi/synd` (anel 3, `kormi=sim`) registra o serviço `"synd"`
  (`spud_svc_serve`) e é o **dono do store userspace de contratos** — semente
  via `SYS_CONTRACT_LIST` no `main` (marker **`[synd] store ring3: N contratos
  userspace (dono da politica: kernel=fallback)`**), trilho de auditoria no
  heap (`synd_aud_*`), espelho exato do modelo authd/fsd (authority do store
  fora do kernel; kernel = primitivo/fallback + enforcement em `vfs_open`).
- **Superfície IPC (`IPC_TYPE_SYN_*`, 30-34):** `LIST` (filtro → texto dos
  contratos vigentes com estado `r w v g a c`), `READ` (caminho → contrato ou
  `"0"`), `CONSENT` (caminho → `'1'`/`'0'`, voluntário), `REVOKE` (caminho →
  `'1'`/`'0'`), `AUDIT` (→ trilho do serviço, mais recente primeiro). Helpers
  `spud_synd_rpc/list/read/consent/revoke/audit` em `spud.h`: **svc-first**;
  só `spud_synd_list` tem fallback syscall (`SYS_CONTRACT_LIST`, normalizado
  varrendo até o NUL).
- **Regra de autorização permanece no kernel como enforcement** (modelo
  authd/fsd): o `synd` decide a política (consentido/revogável) e o kernel
  aplica em `vfs_open`/sessão — autoridade de regras separada da autoridade de
  execução (item 3.2 abaixo).
- **GOTCHA:** `SYS_CONTRACT_LIST` retorna **0** em sucesso (conteúdo no
  buffer, não é tamanho — mesmo do `SYS_FS_LIST`); o seed varre até o NUL e
  parseia `"  - <nome> em <caminho> (r=.. w=.. x=..)"` (o nome pode conter
  espaços). `svctest` exercita list+read → marker `[svctest] synd IPC ok`;
  `_runtest.sh` exige **13 markers** (12º+13º novos).

## 2. Árvore real de decisão (quem chama quem hoje)

```
init (PID 1, kernelspace)
  └─ login_run()      → auth_verify / auth_create_user (senha com PBKDF2)
       └─ shell_run() → vfs_* (leitura/escrita/exec), cap_check
                         synallagma_check_contract() em cada vfs_open
```
Todos os nós estão no mesmo espaço; a "separação" é lógica, não mecânica.

## 3. Synallagma como serviço (Fase M) — plano honesto

### 3.1 Hoje (implementado, verificado)
- Lê `/synallagma/*.pacto` e valida campos (`caminho`, `nome`, `leitura`,
  `escrita`, `execucao`, `voluntario`, `revogavel`).
- Escolhe o contrato mais específico para um caminho acessado.
- Registra consentimento por sessão de login.
- É invocado por **função interna** do kernel no `vfs_open` (primitivo/fallback).
- **Desde ABI 1.6 o store/política é serviço ring 3** (`/kormi/synd`, ver 1.3):
  decisão fora do kernel, enforcement permanece no kernel — item parcialmente
  realizado do 3.2 abaixo.

### 3.2 O que falta para ser "serviço" (Fase M)
O motor atual não roda como processo nem como par de IPC. Para virar serviço:

1. **Extrair** a lógica de `synallagma_*` para um processo ring 0 apartado
   (ou, com o ring 3 maduro, ring 3).
2. **Expor via syscall/IPC**: consumidores enviam `(caminho, sessao)` e o
   serviço responde `(contrato, consentimento, caps)` — nada de `extern`.
3. `vfs_open` passa a ser *cliente* (syscall/IPC) do serviço **ou** o gate fica
   no kernel e o serviço apenas publica as regras (autoridade de regras
   separada da autoridade de execução).
4. Manter a Semântica: contratos voluntários/revogáveis, consentimento por
   sessão — o serviço NUNCA força start; propõe, o usuário aceita.

Decisão de arquitetura recomendada: **autorização como serviço** — kernel faz o
enforcement de uma regra recebida (evita TOCTOU e re-entrada), o serviço
compõe/valida a política. Isso preserva o modelo ancap da mecânica (consenso,
revogação) sem duplicar trusted code no userspace antes dos syscall gates de
ponteiro estarem plenamente testados.

## 4. init/login/shell em ring 3 (Fase N) — plano honesto

### 4.1 Hoje (implementado, verificado)
- `init` (PID 1) chama Login, que chama Shell — no kernel.
- `auth` **já roda fora do kernel**: `authd` (ring 3) como serviço via IPC +
  ticket de sessão (ABI 1.5) — ver 1.1.
- Existe **base de ring 3** (`proc_create_user`, `enter_user`,
  `user_trampoline`, `tss.rsp0` por processo) e apps `/kormi` rodam em anel 3.
- O escalonador é `ret`-baseado; a preempção full-frame/iretq p/ ring 3 **ainda
  não está consolidada** (a troca por syscall funciona).

### 4.2 O que falta para "init/shell reais em ring 3" (Fase N)
1. Scheduler full-frame/iretq: salvar/restaurar frame de usuário por IRQ/syscall
   (não apenas o `ret` cooperativo), para preempção continuar valendo em ring 3.
2. Portar `thais-sh` para um binário ring 3: espaço de endereços próprio, só
   syscalls/IPC, sem `extern` para símbolos do kernel (necessário para `argv`/
   `env` de entrada já funcionantes e ponteiros validados por syscall).
3. `init` real: cria serviços como processos (`fsd`/`authd` via `kormi_start`,
   fs ainda no kernel) via `proc_create*` + contrato Synallagma, em vez de
   chamar funções — o kernel fica com mem/sched/proc/ipc/caps/syscalls.
4. `login` como processo ring 3 consumindo auth-service via IPC (o authd já é
   esse serviço; falta transferir o `login_run` do kernel para a porta do authd).

### 4.3 Próximo passo recomendado (sobre a entrega ABI 1.5)
- **Migrar `fs` para ring 3** (o serviço `/kormi/fsd` **já é dono do store userspace** — `/paradosis` e `/nomos` em cópia no heap do serviço, marker `[fsd] store ring3` — e tem superfície IPC completa READ/LIST/CREATE/WRITE/REMOVE/AUDIT + auditoria; o kernel usa `fs_*`/`vfs_*` em ~93 pontos de uso interno e o **boot/exec já está desacoplado** do ramfs). Correção honesta: a autoridade do filesystem (store + superfície IPC) **já está no serviço** — restando apenas a migração dos usos internos do kernel, que foi **adiada deliberadamente** nesta sessão (ver skill.md §7).

### 4.3 Dependência (ordem sugerida)
```
Fase N (init real) → requer scheduler full-frame/iretq (usermode.asm já tem o frame)
                     → e serviços como processos (auth/fs) já enxuta.
Fase M (Synallagma serviço) → requer IPC consolidado (mailboxes já existem) e
                     processo apartado; independe do ring 3, pode ser ring 0 1º.
```
Recomendado sequenciar **M antes de N** quando a meta é "services consolidados":
o kernel fica menor (tira auth/fs/synallagma) ANTES de empurrar o shell p/ ring 3.

## 5. Critérios de "feito" (acceptance, mensuráveis)

| Item | Critério objetivo |
|---|---|
| authd (auth serviço) | realizado ABI 1.5: `/kormi/authd` dono da store, sessão por ticket, hash via SYS_AUTH_HASH, fallback nos syscalls |
| fs como serviço | processo real `/kormi/fsd` atendendo leitura/escrita por IPC; kernel vira cliente; auditoria via `ps`/IPC |
| Synallagma serviço | processo real com PID, parado ao revogar contrato; auditoria via `ps`/IPC mostra-se como serviço |
| Shell ring 3 | roda com CR3 próprio; `write/read/exec/ipc` só por syscall; crash do shell não derruba kernel |
| init real | PID 1 cria serviços via `proc_create*`, não chama `*_run()` de kernel |
| Login ring 3 | autentica via IPC ao auth-service; hash PBKDF2 permanece no serviço |
| Boot log | marcos visíveis no serial: `init -> services -> shell (ring3) -> login` |
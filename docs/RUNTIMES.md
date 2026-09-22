# Siath OS — Runtimes Híbridos (Python / Node / C++)

## Objetivo: port nativo + compatibilidade Linux/BSD sem simulação

### Fase 1 — Nativo (atual blueprint)
- **libc:** `musl` portado para Siath OS (`/emporion/lib/musl`), wrappers `libthais` traduzem `open/read/write` → `cap_*` + `aisthesis_*`.
- **C++:** `clang++` + `musl` + `libcxx` → binários ELF Thaís (mesmo formato mas syscalls próprias).
- **Python:** CPython 3.12 cross-compilado com `--host=x86_64-thais` usando musl. Módulos precisam de `caps` para FS/rede.
- **Node:** Node 20 cross com `musl`, V8 adaptado para `kinesis_spawn`.

Exemplo em C++ para Siath OS:
```cpp
// /idios/thais/app.cpp
#include <thais/cap.h>
int main(){ thais::cap_grant(CAP_FB_DRAW); thais::fb::draw_text(10,10,"Hello Thaís"); }
```
Compila: `x86_64-thais-g++ app.cpp -o /praxis/app`

### Fase 2 — Compatibilidade Híbrida (WSL-like)
Camada `linux-compat` no kernel: tabela de tradução `linux_syscall -> thais_cap_call`.
Permite rodar binários Linux estáticos (ex: `python3` linux) sem recompilar:
```
linux open("/etc/passwd") → thais cap_has(CAP_NOMOS) ? aisthesis_open : -EPERM
```
Não é simulação: é tradução real de syscalls, como FreeBSD Linuxulator.

### Gráficos
Framebuffer GOP já exposto via `CAP_FB_DRAW`. Runtimes podem criar WM em Node (Electron-like) ou C++ (SFML-like) desenhando direto.

### Build dos runtimes (WSL2)
```bash
make runtimes  # (futuro) baixa musl, compila cpython/node
```

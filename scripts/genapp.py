#!/usr/bin/env python3
"""genapp.py - gera um arquivo .img.h (array C) a partir de um binario.

Se a entrada termina em .asm, compila com `nasm -f bin`; caso contrario, trata
o arquivo como binario cru (ex.: ELF64 de um app userspace compilado por gcc)
e embute os bytes como array C estatico.

Uso:
    python3 scripts/genapp.py <arquivo.asm|arquivo.bin> <simbolo> <saida.h>

Produz o mesmo formato dos headers hello_img.h/ring3demo_img.h/fetch_img.h
(apps de usuario embutidos no kernel e executados em ring 3).
"""
import subprocess
import sys
import os
import tempfile


def main():
    if len(sys.argv) != 4:
        print("uso: genapp.py <asm> <simbolo> <saida.h>", file=sys.stderr)
        return 1

    asm_path = sys.argv[1]
    symbol = sys.argv[2]
    out_path = sys.argv[3]

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tmp:
        bin_path = tmp.name

    try:
        if asm_path.lower().endswith(".asm"):
            subprocess.run(
                ["nasm", "-f", "bin", asm_path, "-o", bin_path],
                check=True,
            )
            with open(bin_path, "rb") as f:
                data = f.read()
        else:
            with open(asm_path, "rb") as f:
                data = f.read()
    finally:
        try:
            os.unlink(bin_path)
        except OSError:
            pass

    rows = []
    for i in range(0, len(data), 14):
        chunk = data[i:i + 14]
        rows.append("  0x%s" % ",0x".join("%02x" % b for b in chunk))

    body = ",\n".join(rows)

    header = (
        "/* %s.h - gerado de %s (nasm -f bin) via scripts/genapp.py.\n"
        "   Binario de usuario embutido para executar em ring 3. NAO editar a mao.\n"
        "   Regenere: python3 scripts/genapp.py %s %s %s\n"
        "*/ static const unsigned char %s[] = {\n"
        "%s\n"
        "};\n"
        "#define %s_SIZE (sizeof(%s))\n"
    ) % (
        os.path.splitext(os.path.basename(out_path))[0],
        os.path.basename(asm_path),
        asm_path,
        symbol,
        out_path,
        symbol,
        body,
        symbol.upper(),
        symbol,
    )

    with open(out_path, "w") as f:
        f.write(header)

    print(f"[genapp] {asm_path} -> {out_path} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

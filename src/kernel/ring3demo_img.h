/* ring3demo_img.h - gerado de src/kernel/ring3demo.asm (nasm -f bin).
   Binario de usuario embutido para o demo de ring 3 (Fase K).
   Regenere com: nasm -f bin src/kernel/ring3demo.asm -o /tmp/r.bin && od -A d -t x1 /tmp/r.bin
   Entry point em 0x400000 (user_vaddr em proc_create_user). */
static const unsigned char ring3demo_img[] = {
  0xb8,0x01,0x00,0x00,0x00, 0xbf,0x01,0x00,0x00,0x00,           /* mov eax,1; mov edi,1 */
  0x48,0x8d,0x35,0x10,0x00,0x00,0x00,                          /* lea rsi,[rel msg] */
  0xba,0x09,0x00,0x00,0x00,                                    /* mov edx,9 */
  0xcd,0x80,                                                   /* int 0x80 (SYS_WRITE) */
  0xb8,0x06,0x00,0x00,0x00,                                    /* mov eax,6 (SYS_YIELD) */
  0xcd,0x80,                                                   /* int 0x80 */
  0xeb,0xf7,                                                   /* jmp -9 (loop) */
  0x52,0x49,0x4e,0x47,0x33,0x2d,0x4f,0x4b,0x00                  /* "RING3-OK\0" (offset 0x21) */
};
#define RING3DEMO_IMG_SIZE (sizeof(ring3demo_img))

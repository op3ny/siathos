#include "thais.h"
#include <stdbool.h>

/* crypto.c — PBKDF2-HMAC-SHA256 (Auditoria Auth).
   Substitui o hashing FNV-1a (nao-cryptografico) por um KDF real com salt +
   custo de iteracoes. Implementacao freestanding sem dependencia de binarios.
   Nao usa hardware de crypto do CPU (fine em QEMU/TCG; aceitavel para login). */

#define SHA256_BLOCK 64
#define SHA256_STATE 32

typedef struct {
    uint32_t h[8];
    uint64_t len;
    uint8_t  buf[SHA256_BLOCK];
    size_t   buflen;
} sha256_ctx_t;

static const uint32_t K[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static inline uint32_t rotr32(uint32_t x,int n){ return (x>>n)|(x<<(32-n)); }
static inline uint32_t be32(const uint8_t*p){ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static inline void put_be32(uint8_t*p,uint32_t v){ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }

static void sha256_compress(sha256_ctx_t *c, const uint8_t *block){
    uint32_t w[64];
    for(int i=0;i<16;i++) w[i]=be32(block+i*4);
    for(int i=16;i<64;i++){
        uint32_t s0=rotr32(w[i-15],7)^rotr32(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=rotr32(w[i-2],17)^rotr32(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    uint32_t a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3];
    uint32_t e=c->h[4],f=c->h[5],g=c->h[6],hh=c->h[7];
    for(int i=0;i<64;i++){
        uint32_t S1=rotr32(e,6)^rotr32(e,11)^rotr32(e,25);
        uint32_t ch=(e&f)^((~e)&g);
        uint32_t t1=hh+S1+ch+K[i]+w[i];
        uint32_t S0=rotr32(a,2)^rotr32(a,13)^rotr32(a,22);
        uint32_t maj=(a&b)^(a&cc)^(b&cc);
        uint32_t t2=S0+maj;
        hh=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;
    c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=hh;
}

static void sha256_init(sha256_ctx_t *c){
    c->h[0]=0x6a09e667;c->h[1]=0xbb67ae85;c->h[2]=0x3c6ef372;c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f;c->h[5]=0x9b05688c;c->h[6]=0x1f83d9ab;c->h[7]=0x5be0cd19;
    c->len=0;c->buflen=0;
}

static void sha256_update(sha256_ctx_t *c, const uint8_t *d, size_t len){
    c->len += len;
    while(len){
        size_t take = SHA256_BLOCK - c->buflen;
        if(take>len) take=len;
        for(size_t i=0;i<take;i++) c->buf[c->buflen+i]=d[i];
        d+=take; len-=take; c->buflen+=take;
        if(c->buflen==SHA256_BLOCK){ sha256_compress(c,c->buf); c->buflen=0; }
    }
}

static void sha256_final(sha256_ctx_t *c, uint8_t out[SHA256_STATE]){
    uint64_t bits = c->len*8;
    /* 0x80, depois zeros ate buflen==56, depois 8 bytes little->big do tamanho */
    c->buf[c->buflen++] = 0x80;
    if(c->buflen > 56){
        while(c->buflen < SHA256_BLOCK){ c->buf[c->buflen++] = 0; }
        sha256_compress(c, c->buf);
        c->buflen = 0;
    }
    while(c->buflen < 56){ c->buf[c->buflen++] = 0; }
    uint8_t bitlen[8];
    put_be32(bitlen,   (uint32_t)(bits>>32));
    put_be32(bitlen+4, (uint32_t)bits);
    for(int i=0;i<8;i++) c->buf[56+i] = bitlen[i];
    sha256_compress(c, c->buf);
    for(int i=0;i<8;i++) put_be32(out+i*4, c->h[i]);
}

static void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, uint8_t out[SHA256_STATE]){
    uint8_t k[SHA256_BLOCK]={0};
    if(klen>SHA256_BLOCK){ sha256_ctx_t t; sha256_init(&t); sha256_update(&t,key,klen); sha256_final(&t,k); }
    else for(size_t i=0;i<klen;i++) k[i]=key[i];
    uint8_t ipad[SHA256_BLOCK], opad[SHA256_BLOCK];
    for(int i=0;i<SHA256_BLOCK;i++){ ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }
    sha256_ctx_t c;
    sha256_init(&c); sha256_update(&c,ipad,SHA256_BLOCK); sha256_update(&c,data,dlen); sha256_final(&c,out);
    sha256_init(&c); sha256_update(&c,opad,SHA256_BLOCK); sha256_update(&c,out,SHA256_STATE); sha256_final(&c,out);
}

void pbkdf2_hmac_sha256(const uint8_t *pw, size_t pwlen, const uint8_t *salt, size_t saltlen,
                        uint32_t iters, uint8_t *out, size_t outlen){
    uint8_t u[SHA256_STATE], t[SHA256_STATE];
    size_t pos=0;
    uint32_t block_i=1;
    while(pos<outlen){
        /* si = salt || INT_BE(block_i); U1 = HMAC(pw, si) */
        uint8_t si[4] = {
            (uint8_t)(block_i>>24),(uint8_t)(block_i>>16),(uint8_t)(block_i>>8),(uint8_t)block_i
        };
        {
            uint8_t concat[128+4];
            size_t pre = saltlen;
            if(pre > 128) pre=128;
            for(size_t k=0;k<pre;k++) concat[k]=salt[k];
            concat[pre]=si[0]; concat[pre+1]=si[1]; concat[pre+2]=si[2]; concat[pre+3]=si[3];
            hmac_sha256(pw,pwlen,concat,pre+4,u);
        }
        for(int j=0;j<SHA256_STATE;j++) t[j]=u[j];
        for(uint32_t r=2;r<=iters;r++){
            hmac_sha256(pw,pwlen,u,SHA256_STATE,u);
            for(int j=0;j<SHA256_STATE;j++) t[j]^=u[j];
        }
        for(int j=0;j<SHA256_STATE && pos<outlen;j++,pos++) out[pos]=t[j];
        block_i++;
    }
}
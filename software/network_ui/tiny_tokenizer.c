#include "tiny_tokenizer.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static uint32_t pair_hash(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9e3779b1u ^ b * 0x85ebca6bu;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    return x;
}
static int section_ok(uint32_t offset, uint32_t count, uint32_t stride, size_t total) {
    return offset >= 128 && !(offset & 3) && offset <= total &&
           (size_t)count <= (total - offset) / stride;
}
int tiny_tokenizer_init(TinyTokenizer *tk, const void *blob, size_t bytes) {
    const uint32_t *h;
    uint32_t slots = 1, i;
    if (!tk) return TINY_TOKENIZER_INVALID;
    memset(tk, 0, sizeof(*tk));
    if (!blob || ((uintptr_t)blob & 3u) || bytes < 128 || memcmp(blob, "QWTOK01\0", 8))
        return TINY_TOKENIZER_INVALID;
    h = (const uint32_t *)blob;
    if (h[2] != 1 || h[3] > bytes || h[3] < 128 || h[4] < 256 ||
        h[4] > 1000000 || h[5] > 2000000) return TINY_TOKENIZER_INVALID;
    bytes = h[3];
    if (!section_ok(h[10],h[4],12,bytes) || !section_ok(h[11],h[18],1,bytes) ||
        !section_ok(h[12],h[5],16,bytes) || !section_ok(h[13],256,4,bytes) ||
        !section_ok(h[14],h[6],12,bytes) || !section_ok(h[15],h[7],12,bytes) ||
        !section_ok(h[16],h[8],16,bytes) || !section_ok(h[17],h[9],12,bytes))
        return TINY_TOKENIZER_INVALID;
    tk->blob = (const unsigned char *)blob;
    tk->vocab_size=h[4]; tk->merge_count=h[5]; tk->category_count=h[6];
    tk->ccc_count=h[7]; tk->decomp_count=h[8]; tk->compose_count=h[9];
    tk->tokens=(const uint32_t *)(tk->blob+h[10]); tk->token_data=tk->blob+h[11];
    tk->merges=(const uint32_t *)(tk->blob+h[12]); tk->byte_ids=(const uint32_t *)(tk->blob+h[13]);
    tk->categories=(const uint32_t *)(tk->blob+h[14]); tk->ccc=(const uint32_t *)(tk->blob+h[15]);
    tk->decomp=(const uint32_t *)(tk->blob+h[16]); tk->compose=(const uint32_t *)(tk->blob+h[17]);
    for(i=0;i<tk->vocab_size;i++) {
        const uint32_t *r=tk->tokens+3*i;
        if(r[0]>h[18] || r[1]>h[18]-r[0]) { memset(tk,0,sizeof(*tk)); return TINY_TOKENIZER_INVALID; }
    }
    while(slots < tk->merge_count*2) slots <<= 1;
    tk->hash_mask=slots-1;
    tk->merge_hash=(uint32_t *)calloc(slots,sizeof(uint32_t));
    if(!tk->merge_hash) { memset(tk,0,sizeof(*tk)); return TINY_TOKENIZER_NOMEM; }
    for(i=0;i<tk->merge_count;i++) {
        const uint32_t *r=tk->merges+4*i;
        uint32_t p;
        if(r[0]>=tk->vocab_size || r[1]>=tk->vocab_size || r[2]>=tk->vocab_size) {
            tiny_tokenizer_destroy(tk); return TINY_TOKENIZER_INVALID;
        }
        p=pair_hash(r[0],r[1])&tk->hash_mask;
        while(tk->merge_hash[p]) p=(p+1)&tk->hash_mask;
        tk->merge_hash[p]=i+1;
    }
    return 0;
}
void tiny_tokenizer_destroy(TinyTokenizer *tk) {
    if(tk) { free(tk->merge_hash); memset(tk,0,sizeof(*tk)); }
}
int tiny_tokenizer_token_bytes(const TinyTokenizer *tk,int32_t token,
    const unsigned char **bytes,size_t *length,int *special) {
    const uint32_t *r;
    if(!tk || !tk->blob || token<0 || (uint32_t)token>=tk->vocab_size || !bytes || !length)
        return TINY_TOKENIZER_INVALID;
    r=tk->tokens+3*(uint32_t)token; *bytes=tk->token_data+r[0]; *length=r[1];
    if(special) *special=!!(r[2]&2u);
    return 0;
}
static const uint32_t *merge_find(const TinyTokenizer *tk,uint32_t a,uint32_t b) {
    uint32_t p=pair_hash(a,b)&tk->hash_mask;
    while(tk->merge_hash[p]) {
        const uint32_t *r=tk->merges+4*(tk->merge_hash[p]-1);
        if(r[0]==a && r[1]==b) return r;
        p=(p+1)&tk->hash_mask;
    }
    return NULL;
}
static uint32_t range_value(const uint32_t *table,uint32_t n,uint32_t cp) {
    uint32_t lo=0,hi=n;
    while(lo<hi) { uint32_t m=lo+(hi-lo)/2; const uint32_t *r=table+3*m;
        if(cp<r[0]) hi=m; else if(cp>r[1]) lo=m+1; else return r[2]; }
    return 0;
}
static uint32_t flags(const TinyTokenizer *tk,uint32_t cp) { return range_value(tk->categories,tk->category_count,cp); }
static uint32_t ccc(const TinyTokenizer *tk,uint32_t cp) { return range_value(tk->ccc,tk->ccc_count,cp); }
static int utf8_read(const unsigned char *s,size_t n,uint32_t *cp) {
    unsigned c; int k,i; uint32_t v;
    if(!n) return 0;
    c=s[0]; if(c<128) { *cp=c; return 1; }
    if(c>=0xc2 && c<=0xdf) { k=2;v=c&31; }
    else if(c>=0xe0 && c<=0xef) { k=3;v=c&15; }
    else if(c>=0xf0 && c<=0xf4) { k=4;v=c&7; }
    else { *cp=0xfffd;return 1; }
    if(n<(size_t)k) { *cp=0xfffd;return 1; }
    for(i=1;i<k;i++) { if((s[i]&0xc0)!=0x80) { *cp=0xfffd;return 1; } v=(v<<6)|(s[i]&63); }
    if((k==2&&v<0x80)||(k==3&&v<0x800)||(k==4&&v<0x10000)||v>0x10ffff||(v>=0xd800&&v<=0xdfff)) { *cp=0xfffd;return 1; }
    *cp=v;return k;
}
static unsigned utf8_write(uint32_t cp,unsigned char *o) {
    if(cp<128) { o[0]=(unsigned char)cp;return 1; }
    if(cp<0x800) { o[0]=(unsigned char)(0xc0|(cp>>6));o[1]=(unsigned char)(0x80|(cp&63));return 2; }
    if(cp<0x10000) { o[0]=(unsigned char)(0xe0|(cp>>12));o[1]=(unsigned char)(0x80|((cp>>6)&63));o[2]=(unsigned char)(0x80|(cp&63));return 3; }
    o[0]=(unsigned char)(0xf0|(cp>>18));o[1]=(unsigned char)(0x80|((cp>>12)&63));o[2]=(unsigned char)(0x80|((cp>>6)&63));o[3]=(unsigned char)(0x80|(cp&63));return 4;
}
typedef struct { uint32_t *data;size_t size,capacity; } Codepoints;
static int cp_push(Codepoints *v,uint32_t cp) {
    if(v->size==v->capacity) {
        size_t cap=v->capacity?v->capacity*2:128;
        uint32_t *p;
        if(cap<v->capacity || cap>SIZE_MAX/sizeof(uint32_t)) return -2;
        p=(uint32_t *)realloc(v->data,cap*sizeof(uint32_t));if(!p) return -2;
        v->data=p;v->capacity=cap;
    }
    v->data[v->size++]=cp;return 0;
}
static int decompose(const TinyTokenizer *tk,Codepoints *v,uint32_t cp) {
    uint32_t lo=0,hi=tk->decomp_count;
    if(cp>=0xac00 && cp<0xac00+11172) {
        uint32_t s=cp-0xac00;
        if(cp_push(v,0x1100+s/588)||cp_push(v,0x1161+(s%588)/28)) return -2;
        return (s%28)?cp_push(v,0x11a7+s%28):0;
    }
    while(lo<hi) { uint32_t m=lo+(hi-lo)/2;const uint32_t *r=tk->decomp+4*m;
        if(cp<r[0]) hi=m; else if(cp>r[0]) lo=m+1; else {
            if(decompose(tk,v,r[1])) return -2;
            return r[3]==2?decompose(tk,v,r[2]):0;
        }
    }
    return cp_push(v,cp);
}
static uint32_t compose_pair(const TinyTokenizer *tk,uint32_t a,uint32_t b) {
    uint32_t lo=0,hi=tk->compose_count;
    if(a>=0x1100&&a<0x1113&&b>=0x1161&&b<0x1176) return 0xac00+(a-0x1100)*588+(b-0x1161)*28;
    if(a>=0xac00&&a<0xac00+11172&&!((a-0xac00)%28)&&b>0x11a7&&b<0x11c3) return a+b-0x11a7;
    while(lo<hi) { uint32_t m=lo+(hi-lo)/2;const uint32_t *r=tk->compose+3*m;
        if(a<r[0]||(a==r[0]&&b<r[1])) hi=m;
        else if(a>r[0]||(a==r[0]&&b>r[1])) lo=m+1;
        else return r[2]; }
    return 0;
}
static int normalize(const TinyTokenizer *tk,const unsigned char *s,size_t n,Codepoints *v) {
    size_t p=0,i,j,out,starter=0;uint32_t last=0;
    v->size=0;
    while(p<n) { uint32_t cp;int len=utf8_read(s+p,n-p,&cp);p+=(size_t)len;
        if(decompose(tk,v,cp)) return -2; }
    /* Canonical ordering is stable, and never moves a mark over a starter. */
    for(i=1;i<v->size;i++) { uint32_t cls=ccc(tk,v->data[i]),cp=v->data[i];j=i;
        if(cls) { while(j && ccc(tk,v->data[j-1])>cls) { v->data[j]=v->data[j-1];j--; }v->data[j]=cp; } }
    if(!v->size) return 0;
    out=1;last=ccc(tk,v->data[0]);
    for(i=1;i<v->size;i++) {
        uint32_t cp=v->data[i],cls=ccc(tk,cp),merged=compose_pair(tk,v->data[starter],cp);
        if(merged && (last<cls||last==0)) v->data[starter]=merged;
        else { if(!cls) starter=out;v->data[out++]=cp;last=cls; }
    }
    v->size=out;return 0;
}
static uint32_t ascii_lower(uint32_t c) { return c>='A'&&c<='Z'?c+32:c; }
/* Exact ordered alternatives of the regex stored in this model's tokenizer.json. */
static size_t piece_end(const TinyTokenizer *tk,const uint32_t *s,size_t n,size_t p) {
    size_t q,j;uint32_t f=flags(tk,s[p]);
    if(s[p]=='\'' && p+1<n) {
        uint32_t c=ascii_lower(s[p+1]);
        if(c=='s'||c=='t'||c=='m'||c=='d') return p+2;
        if(p+2<n && ((c=='r'&&ascii_lower(s[p+2])=='e') || (c=='v'&&ascii_lower(s[p+2])=='e') || (c=='l'&&ascii_lower(s[p+2])=='l'))) return p+3;
    }
    q=p;
    if(!(f&3) && s[p]!='\r' && s[p]!='\n' && p+1<n && (flags(tk,s[p+1])&1)) q++;
    if(flags(tk,s[q])&1) { do { q++; } while(q<n && (flags(tk,s[q])&1));return q; }
    if(f&2) return p+1;
    q=p;if(s[q]==' ' && q+1<n) q++;
    if(!(flags(tk,s[q])&7)) {
        do { q++; } while(q<n && !(flags(tk,s[q])&7));
        while(q<n && (s[q]=='\r'||s[q]=='\n')) q++;
        return q;
    }
    if(f&4) {
        q=p;while(q<n&&(flags(tk,s[q])&4)) q++;
        /* Whitespace with a final newline: greedy \\s* backtracks to its last CR/LF. */
        j=q;while(j>p) { if(s[j-1]=='\r'||s[j-1]=='\n') return j;j--; }
        /* \\s+(?!\\S) keeps all trailing space, but one less before a nonspace. */
        if(q==n) return q;
        if(q-p>1) return q-1;
        return q;
    }
    return p+1;
}
static void emit_id(uint32_t id,int32_t *ids,size_t cap,size_t *count) {
    if(*count<cap && ids) ids[*count]=(int32_t)id;
    (*count)++;
}
static int bpe_piece(const TinyTokenizer *tk,const uint32_t *cp,size_t n,int32_t *ids,size_t cap,size_t *count) {
    uint32_t *symbols;size_t len=0,i,j,limit;
    unsigned char bytes[4]={0};
    if(n>SIZE_MAX/(4*sizeof(uint32_t))) return -2;
    limit=n*4;symbols=(uint32_t *)malloc((limit?limit:1)*sizeof(uint32_t));
    if(!symbols) return -2;
    for(i=0;i<n;i++) { unsigned k,m=utf8_write(cp[i],bytes);for(k=0;k<m;k++) symbols[len++]=tk->byte_ids[bytes[k]]; }
    while(len>1) {
        uint32_t rank=UINT32_MAX,merged=0;size_t best=SIZE_MAX;
        for(i=0;i+1<len;i++) { const uint32_t *r=merge_find(tk,symbols[i],symbols[i+1]);
            if(r&&r[3]<rank) { rank=r[3];merged=r[2];best=i; } }
        if(best==SIZE_MAX) break;
        symbols[best]=merged;
        for(j=best+1;j+1<len;j++) symbols[j]=symbols[j+1];
        len--;
    }
    for(i=0;i<len;i++) emit_id(symbols[i],ids,cap,count);
    free(symbols);return 0;
}
static int encode_plain(const TinyTokenizer *tk,const unsigned char *s,size_t n,int32_t *ids,size_t cap,size_t *count,Codepoints *v) {
    size_t p=0;
    if(normalize(tk,s,n,v)) return -2;
    while(p<v->size) { size_t end=piece_end(tk,v->data,v->size,p);
        if(bpe_piece(tk,v->data+p,end-p,ids,cap,count)) return -2;
        p=end; }
    return 0;
}
int tiny_tokenizer_encode(TinyTokenizer *tk,const char *text,size_t bytes,int32_t *ids,size_t cap,size_t *count) {
    const unsigned char *s=(const unsigned char *)text;
    Codepoints v={0};size_t p=0,plain=0,total=0;int error=0;
    if(!tk||!tk->blob||(!s&&bytes)||!count||(!ids&&cap)) return -1;
    while(p<bytes) {
        uint32_t found=UINT32_MAX,i;size_t longest=0;
        if(s[p]=='<') for(i=0;i<tk->vocab_size;i++) {
            const uint32_t *r=tk->tokens+3*i;
            if((r[2]&1)&&r[1]<=bytes-p&&r[1]>longest&&!memcmp(s+p,tk->token_data+r[0],r[1])) {found=i;longest=r[1];}
        }
        if(found!=UINT32_MAX) {
            if(p>plain && encode_plain(tk,s+plain,p-plain,ids,cap,&total,&v)) { error=-2;break; }
            emit_id(found,ids,cap,&total);p+=longest;plain=p;
        } else p++;
    }
    if(!error && p>plain) error=encode_plain(tk,s+plain,p-plain,ids,cap,&total,&v);
    free(v.data);*count=total;
    return error?error:(total>cap?-3:0);
}

size_t tiny_utf8_stream_push(TinyUtf8Stream *st,const unsigned char *s,size_t n,int finish,char *o,size_t cap) {
    size_t p=0,out=0;
    if(!st||(!s&&n)||!o||st->length>4) return SIZE_MAX;
    while(p<n || (finish&&st->length)) {
        uint32_t cp=0xfffd;int want,k;
        if(!st->length) st->pending[st->length++]=s[p++];
        want=st->pending[0]<0x80?1:st->pending[0]>=0xc2&&st->pending[0]<=0xdf?2:st->pending[0]>=0xe0&&st->pending[0]<=0xef?3:st->pending[0]>=0xf0&&st->pending[0]<=0xf4?4:1;
        while(st->length<want && p<n && (s[p]&0xc0)==0x80) st->pending[st->length++]=s[p++];
        if(st->length<want && p==n&&!finish) break;
        k=utf8_read(st->pending,st->length,&cp);
        if(cp==0xfffd && !(st->length==3 && st->pending[0]==0xef&&st->pending[1]==0xbf&&st->pending[2]==0xbd)) {
            if(cap-out<3) return SIZE_MAX;
            o[out++]=(char)0xef;o[out++]=(char)0xbf;o[out++]=(char)0xbd;
            /* A valid prefix truncated at EOF is one replacement, as UTF8 decoding. */
            if(st->length<want) k=st->length;
        } else {
            if(cap-out<(size_t)k) return SIZE_MAX;
            memcpy(o+out,st->pending,(size_t)k);out+=(size_t)k;
        }
        st->length=(unsigned char)(st->length-k);
        if(st->length) memmove(st->pending,st->pending+k,st->length);
    }
    return out;
}

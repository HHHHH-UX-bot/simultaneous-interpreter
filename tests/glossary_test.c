/* Host test of software/network_ui/glossary.c. Prints the session messages it
   builds, one per line, for scripts/test_glossary.py to parse as JSON. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../software/network_ui/glossary.h"

static const char base[]="{\"type\":\"session.update\",\"session\":{\"modalities\":[\"text\"],"
    "\"translation\":{\"language\":\"en\",\"same_language_skip_options\":{\"skip_text\":false,\"skip_audio\":false}}}}";

int main(void) {
    char out[2048];uint32_t n;
    /* No pairs: the session goes unchanged. */
    assert(glossary_load((const uint8_t *)"",0)==0u);
    n=glossary_session(base,sizeof(base)-1u,0,out,sizeof(out));
    assert(n==sizeof(base)-1u && !memcmp(out,base,n));
    /* BOM, comments, blanks and CRLF; a line without '=', an empty side and
       invalid UTF-8 are skipped; quotes and backslashes are escaped. */
    static const char file[]="\xef\xbb\xbf# glossary\n\n  \xe4\xbc\x9a\xe8\xae\xae  =  meeting \r\n"
        "no separator\n= empty\n\xff\xfe = broken\n"
        "\xe5\xbc\x95\xe5\x8f\xb7 = say \"hi\" \\ bye\nFPGA=FPGA\n";
    assert(glossary_load((const uint8_t *)file,sizeof(file)-1u)==3u && glossary_skipped()==3u);
    for(int english=0;english<2;++english) {
        n=glossary_session(base,sizeof(base)-1u,english,out,sizeof(out));
        assert(n>sizeof(base));out[n]=0;puts(out);
    }
    /* The budget caps what is accepted, so a full session always fits one frame. */
    static char many[GLOSSARY_FILE_BYTES];uint32_t used=0;
    for(uint32_t i=0;used+40u<sizeof(many);++i) used+=(uint32_t)snprintf(many+used,sizeof(many)-used,"term%03u = translation%03u\n",i,i);
    uint32_t accepted=glossary_load((const uint8_t *)many,used);
    assert(accepted>20u && accepted<GLOSSARY_PAIRS && glossary_skipped()>0u);
    n=glossary_session(base,sizeof(base)-1u,0,out,sizeof(out));
    assert(n && n-(sizeof(base)-1u)+3u<=GLOSSARY_JSON_BUDGET+3u);out[n]=0;puts(out);
    /* The shipped example loads whole (run from the project root). */
    static uint8_t example[GLOSSARY_FILE_BYTES];
    FILE *f=fopen("assets/glossary.example.txt","rb");assert(f);
    uint32_t example_bytes=(uint32_t)fread(example,1,sizeof(example),f);fclose(f);
    assert(glossary_load(example,example_bytes)==5u && !glossary_skipped());
    /* Wrong shape or too small a buffer: nothing is produced. */
    assert(!glossary_session("{\"a\":1}",7,0,out,sizeof(out)));
    assert(!glossary_session(base,sizeof(base)-1u,0,out,200));
    printf("PASS_GLOSSARY accepted_from_4k=%u session_bytes=%u\n",(unsigned)accepted,(unsigned)n);
    return 0;
}

#include "sauti_library.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    char name[80];
    assert(sauti_library_name("My Music.m3u",name,sizeof(name)));
    assert(strcmp(name,"My Music")==0);
    assert(!sauti_library_name("../evil.m3u",name,sizeof(name)));
    assert(!sauti_library_name("music.mp3",name,sizeof(name)));
    assert(!sauti_library_name("bad/name.m3u",name,sizeof(name)));
    char names[3][80]={{"Rock"},{"Jazz"},{"Ambient"}};
    assert(sauti_library_find(&names[0][0],80,3,"Jazz")==1);
    assert(sauti_library_find(&names[0][0],80,3,"Missing")==-1);
    char input[12]="Hi";
    assert(sauti_library_append_name(input,sizeof(input)," /Rock!") == 7);
    assert(strcmp(input,"Hi Rock")==0);
    sauti_library_backspace(input);
    assert(strcmp(input,"Hi Roc")==0);
    puts("Sauti library tests passed");
    return 0;
}

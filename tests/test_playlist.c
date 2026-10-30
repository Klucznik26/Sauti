#include "sauti_playlist.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int keep(const char *path,void *unused){
    (void)unused;
    return strcmp(path,"/music/missing.mp3")!=0;
}
int main(void){
    char tracks[4][128]={{0}};
    int count=0;
    assert(sauti_queue_add(&tracks[0][0],sizeof(tracks[0]),&count,4,"/music/a.mp3"));
    assert(!sauti_queue_add(&tracks[0][0],sizeof(tracks[0]),&count,4,"/music/a.mp3"));
    assert(sauti_queue_add(&tracks[0][0],sizeof(tracks[0]),&count,4,"/music/b.mp3"));
    FILE *fp=tmpfile();assert(fp);
    assert(sauti_playlist_write(fp,&tracks[0][0],sizeof(tracks[0]),count));
    rewind(fp);
    char restored[4][128]={{0}};int loaded=0;
    assert(sauti_playlist_read(fp,&restored[0][0],sizeof(restored[0]),&loaded,4,keep,NULL));
    assert(loaded==2&&!strcmp(restored[0],"/music/a.mp3"));
    fclose(fp);
    char path[128];
    assert(sauti_queue_take(&tracks[0][0],sizeof(tracks[0]),&count,0,path,sizeof(path)));
    assert(!strcmp(path,"/music/a.mp3")&&count==1);
    assert(!strcmp(tracks[0],"/music/b.mp3"));
    assert(sauti_queue_remove(&tracks[0][0],sizeof(tracks[0]),&count,0)&&count==0);
    fp=tmpfile();assert(fp);
    fputs("#EXTM3U\n/music/missing.mp3\n/music/b.mp3\n",fp);rewind(fp);
    assert(sauti_playlist_read(fp,&restored[0][0],sizeof(restored[0]),&loaded,4,keep,NULL));
    assert(loaded==1&&!strcmp(restored[0],"/music/b.mp3"));
    fclose(fp);
    /* A final path without CR/LF must not disappear. */
    fp=tmpfile();assert(fp);
    assert(fputs("#EXTM3U\n/music/a.mp3",fp)>=0);
    rewind(fp);
    assert(sauti_playlist_read(fp,&restored[0][0],sizeof(restored[0]),&loaded,4,keep,NULL));
    assert(loaded==1&&!strcmp(restored[0],"/music/a.mp3"));
    fclose(fp);
    puts("Sauti portable playlist tests passed");
    return 0;
}

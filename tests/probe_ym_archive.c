#include "sauti_ym_archive.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"usage: %s file.YM\n",argv[0]);return 2;}
 FILE *f=fopen(argv[1],"rb");
 if(!f){perror(argv[1]);return 2;}
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return 2;}
 long n=ftell(f);
 if(n<=0||n>16*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return 2;}
 uint8_t *in=malloc((size_t)n);
 if(!in){fclose(f);return 2;}
 if(fread(in,1,(size_t)n,f)!=(size_t)n){perror("read");fclose(f);free(in);return 2;}
 fclose(f);
 printf("Input: %ld bytes, prefix:",n);
 for(int i=0;i<16&&i<n;i++)printf(" %02x",in[i]);
 putchar('\n');
 uint8_t *out=NULL;size_t size=0;
 int result=sauti_ym_unpack_lha(in,(size_t)n,&out,&size);
 printf("LHA result: %d; extracted bytes: %zu\n",result,size);
 if(result==1)printf("Raw YM: %.4s\n",(char *)out);
 free(out);free(in);
 return result==1?0:1;
}

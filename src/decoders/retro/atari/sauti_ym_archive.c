/* In-memory and bounded extraction of a single YM track from LHA/LH5.
 * No external command, filesystem writes or archive-path interpretation. */
#include "sauti_ym_archive.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SAUTI_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif
#define MAX_YM_ARCHIVE (16u*1024u*1024u)
#define MAX_YM_UNPACKED (32u*1024u*1024u)
#ifdef SAUTI_HAVE_LIBARCHIVE
static int is_raw_ym(const uint8_t *p,size_t n){
 return p && ((n>=4&&(!memcmp(p,"YM2!",4)||!memcmp(p,"YM3!",4)))||
  (n>=12&&(!memcmp(p,"YM5!LeOnArD!",12)||!memcmp(p,"YM6!LeOnArD!",12))));
}
#endif
int sauti_ym_unpack_lha(const uint8_t *data,size_t size,
                        uint8_t **out,size_t *out_size){
 if(!out||!out_size)return -1;
 *out=NULL;*out_size=0;
 if(!data||size<7||memcmp(data+2,"-lh",3)!=0||data[6]!='-')return 0;
 if(size>MAX_YM_ARCHIVE)return -1;
#ifndef SAUTI_HAVE_LIBARCHIVE
 return -1;
#else
 struct archive *a=archive_read_new();
 if(!a)return -1;
 archive_read_support_filter_none(a);
 archive_read_support_format_lha(a);
 int ok=-1;
 if(archive_read_open_memory(a,data,size)!=ARCHIVE_OK){
  fprintf(stderr,"Sauti LHA: cannot open archive: %s\n",archive_error_string(a));goto done;
 }
 struct archive_entry *entry=NULL;
 if(archive_read_next_header(a,&entry)!=ARCHIVE_OK){
  fprintf(stderr,"Sauti LHA: cannot read first entry: %s\n",archive_error_string(a));goto done;
 }
 if(archive_entry_filetype(entry)!=AE_IFREG && archive_entry_filetype(entry)!=0){
  fprintf(stderr,"Sauti LHA: first entry is not a regular file\n");goto done;
 }
 int64_t length=archive_entry_size(entry);
 if(length<=0||length>MAX_YM_UNPACKED){
  fprintf(stderr,"Sauti LHA: invalid member length: %lld\n",(long long)length);goto done;
 }
 uint8_t *buf=malloc((size_t)length);
 if(!buf)goto done;
 size_t used=0;
 while(used<(size_t)length){
  la_ssize_t got=archive_read_data(a,buf+used,(size_t)length-used);
  if(got<=0){
   fprintf(stderr,"Sauti LHA: decode stopped at %zu/%lld: %s\n",used,(long long)length,archive_error_string(a));
   free(buf);goto done;
  }
  used+=(size_t)got;
 }
 /* Reject extra file entries; raw YM is validated again by its decoder. */
 if(!is_raw_ym(buf,used)){
  fprintf(stderr,"Sauti LHA: extracted member is not raw YM2/3/5/6\n");
  free(buf);goto done;
 }
 int tail=archive_read_next_header(a,&entry);
 if(tail!=ARCHIVE_EOF){
  fprintf(stderr,"Sauti LHA: extra member or bad trailer (status %d): %s\n",tail,archive_error_string(a));
  free(buf);goto done;
 }
 *out=buf;*out_size=used;ok=1;
done:
 archive_read_free(a);
 return ok;
#endif
}

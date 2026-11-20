/* Zapisuje syntetyczne pliki wszystkich formatow (tests/mks_probe_corpus.h) do katalogu: korpus poczatkowy dla fuzzingu (bez danych uzytkownika).  usage: dump_corpus OUT_DIR */
#include "mks_probe_corpus.h"
#include <stdio.h>
int main(int argc, char **argv) {
    corpus_t c; size_t i; char path[1024];
    if (argc < 2) return 2;
    memset(&c, 0, sizeof c);
    corpus_add_synthetic(&c);
    for (i = 0; i < c.count; i++) {
        FILE *f; snprintf(path, sizeof path, "%s/syn_%02zu_%s", argv[1], i, c.items[i].name);
        f = fopen(path, "wb"); if (!f) return 1;
        if (c.items[i].n) fwrite(c.items[i].bytes, 1, c.items[i].n, f);
        fclose(f);
    }
    corpus_free(&c);
    printf("dumped %zu synthetic files\n", i);
    return 0;
}

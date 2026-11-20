/* Uruchamia cel fuzzingu na plikach z linii polecen (powtorka korpusu) bez libFuzzera: do lokalnej weryfikacji gcc+ASan/UBSan i do zwyklego CI.  usage: PROG FILE_OR_DIR... */
#include "fuzz_common.h"
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
static int run_file(const char *path) {
    FILE *f = fopen(path, "rb"); long n; uint8_t *b;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return 0; }
    b = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n) LLVMFuzzerTestOneInput(b, (size_t)n);
    free(b); fclose(f);
    return 1;
}
int main(int argc, char **argv) {
    int i, count = 0;
    for (i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            DIR *d = opendir(argv[i]); struct dirent *e;
            while (d && (e = readdir(d)) != NULL) { char path[1024]; if (e->d_name[0] == '.') continue; snprintf(path, sizeof path, "%s/%s", argv[i], e->d_name); count += run_file(path); }
            if (d) closedir(d);
        } else count += run_file(argv[i]);
    }
    printf("fuzz replay: %d inputs, no failure\n", count);
    return 0;
}

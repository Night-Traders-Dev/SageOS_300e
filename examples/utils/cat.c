#include <stdio.h>

void cat_file(FILE *f) {
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
}

int main(int argc, char *argv[]) {
    if (argc == 1) {
        cat_file(stdin);
    } else {
        for (int i = 1; i < argc; i++) {
            FILE *f = fopen(argv[i], "rb");
            if (!f) {
                perror(argv[i]);
                continue;
            }
            cat_file(f);
            fclose(f);
        }
    }
    return 0;
}

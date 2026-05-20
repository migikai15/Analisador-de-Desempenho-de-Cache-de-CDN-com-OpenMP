#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINES 15000000

void extract_url(const char* log_line, char* url) {
    const char* p = strchr(log_line, '"');
    if (p) {
        sscanf(p + 1, "%*s %255s", url);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: %s <arquivo_de_log>\n", argv[0]);
        return 1;
    }
    
    HashTable* ht = ht_create(HASH_SIZE);
    
    FILE* mf = fopen("manifest.txt", "r");
    if (mf) {
        char line[512];
        while (fgets(line, sizeof(line), mf)) {
            line[strcspn(line, "\r\n")] = 0;
            ht_put(ht, line);
        }
        fclose(mf);
    }
    
    FILE* lf = fopen(argv[1], "r");
    if (!lf) return 1;
    
    char** lines = malloc(MAX_LINES * sizeof(char*));
    int count = 0;
    char buffer[512];
    
    while (fgets(buffer, sizeof(buffer), lf) && count < MAX_LINES) {
        lines[count] = strdup(buffer);
        count++;
    }
    fclose(lf);
    
    #pragma omp parallel for
    for (int i = 0; i < count; i++) {
        char url[256];
        extract_url(lines[i], url);
        CacheNode* node = ht_get(ht, url);
        if (node) {
            #pragma omp atomic update
            node->hit_count++;
        }
        free(lines[i]);
    }
    free(lines);
    
    ht_save_results(ht, "results.csv");
    ht_destroy(ht);
    
    return 0;
}
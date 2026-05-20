#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash_table.h"

#define HASH_SIZE 131071

//extrair a URL 
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
            line[strcspn(line, "\r\n")] = 0; // remove newline
            ht_put(ht, line);
        }
        fclose(mf);
    } else {
        printf("Erro ao abrir manifest.txt\n");
        return 1;
    }
    
    FILE* lf = fopen(argv[1], "r");
    if (!lf) {
        printf("Erro ao abrir %s\n", argv[1]);
        return 1;
    }
    
    char line[512];
    char url[256];
    while (fgets(line, sizeof(line), lf)) {
        extract_url(line, url);
        CacheNode* node = ht_get(ht, url);
        if (node) {
            node->hit_count++;
        }
    }
    fclose(lf);
    
    ht_save_results(ht, "results.csv");
    ht_destroy(ht);
    
    return 0;
}
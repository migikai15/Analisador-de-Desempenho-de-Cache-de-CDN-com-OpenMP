#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>
#include "hash_table.h"

#define HASH_SIZE 131071
#define MAX_LINES 15000000

// copia local da função hash para mapearmos as requisicoes corretamente para seus respectivos buckets
size_t hash_djb2_local(const char* str, size_t size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % size;
}

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
    
    // inicializacao dos Locks
    omp_lock_t* locks = malloc(sizeof(omp_lock_t) * HASH_SIZE);
    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_init_lock(&locks[i]);
    }
    
    #pragma omp parallel for
    for (int i = 0; i < count; i++) {
        char url[256];
        extract_url(lines[i], url);
        CacheNode* node = ht_get(ht, url);
        if (node) {
            size_t bucket = hash_djb2_local(url, HASH_SIZE);
            
            omp_set_lock(&locks[bucket]);
            node->hit_count++;
            omp_unset_lock(&locks[bucket]);
        }
        free(lines[i]);
    }
    free(lines);
    
    // destruicao dos locks
    for (size_t i = 0; i < HASH_SIZE; i++) {
        omp_destroy_lock(&locks[i]);
    }
    free(locks);
    
    ht_save_results(ht, "results.csv");
    ht_destroy(ht);
    
    return 0;
}
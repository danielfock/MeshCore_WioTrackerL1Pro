#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MAX_TKS_ENTRIES 16

int main() {
    uint16_t cache_ids[MAX_TKS_ENTRIES];
    for (int i=0; i<MAX_TKS_ENTRIES; i++) cache_ids[i] = i;

    memmove(&cache_ids[0], &cache_ids[1], (MAX_TKS_ENTRIES - 1) * sizeof(cache_ids[0]));
    cache_ids[MAX_TKS_ENTRIES - 1] = 99;

    for (int i=0; i<MAX_TKS_ENTRIES; i++) {
        printf("%d ", cache_ids[i]);
    }
    printf("\n");
    return 0;
}

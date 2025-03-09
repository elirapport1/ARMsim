#ifndef _CACHE_H_
#define _CACHE_H_

#include <stdint.h>

typedef struct {
    int valid;
    uint64_t tag;
    int lru;           // or some integer to track usage; smaller = more recently used
    // Optionally: uint8_t block_data[32];
} cache_line_t;

typedef struct {
    cache_line_t *lines;  // array of 'ways' lines
} cache_set_t;

typedef struct
{
    int sets;         // e.g. 64 for I-cache, 256 for D-cache
    int ways;         // e.g. 4 for I-cache, 8 for D-cache
    int block_size;   // 32 for both
    int offset_bits;  // log2(block_size)
    int index_bits;   // log2(sets)
    cache_set_t *set; // dynamically allocated array of sets

} cache_t;

cache_t *cache_new(int sets, int ways, int block);
void cache_destroy(cache_t *c);
int cache_update(cache_t *c, uint64_t addr);
int cache_check_hit_only(cache_t *c, uint64_t addr);

#endif

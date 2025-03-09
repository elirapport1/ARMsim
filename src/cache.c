#include "cache.h"
#include <stdlib.h>
#include <stdio.h>

cache_t *cache_new(int sets, int ways, int block)
{
    cache_t *c = malloc(sizeof(cache_t));
    c->sets = sets;
    c->ways = ways;
    c->block_size = block;

    // offset_bits = log2(block)
    // index_bits = log2(sets)
    // For an integer power-of-two, can do a small loop or use built-ins:

    c->offset_bits = 0;
    int tmp = block;
    while (tmp > 1) { tmp >>= 1; c->offset_bits++; }

    c->index_bits = 0;
    tmp = sets;
    while (tmp > 1) { tmp >>= 1; c->index_bits++; }

    c->set = malloc(sizeof(cache_set_t) * sets);

    for (int s = 0; s < sets; s++) {
        c->set[s].lines = calloc(ways, sizeof(cache_line_t));
        // This zero-initializes 'valid=0, tag=0, lru=0', etc.
    }

    return c;
}

void cache_destroy(cache_t *c)
{
    for (int s = 0; s < c->sets; s++) {
        free(c->set[s].lines);
    }
    free(c->set);
    free(c);
}


int cache_update(cache_t *c, uint64_t addr)
{
  // 1) Extract index, tag
    uint64_t index = (addr >> c->offset_bits) & ((1ULL << c->index_bits) - 1ULL);
    uint64_t tag   = addr >> (c->offset_bits + c->index_bits);

    cache_set_t *set = &c->set[index];

    // 2) Search for matching line
    for (int w = 0; w < c->ways; w++) {
        cache_line_t *line = &set->lines[w];
        if (line->valid && line->tag == tag) {
            // Hit: update LRU, return 1
            // (Similar to above; line->lru=0, etc.)
            return 1; 
        }
    }

    // 3) Miss => find empty line or LRU victim
    int victim_way = -1;
    int max_lru    = -1;
    for (int w = 0; w < c->ways; w++) {
        if (!set->lines[w].valid) {
            victim_way = w;
            break;
        }
        if (set->lines[w].lru > max_lru) {
            max_lru = set->lines[w].lru;
            victim_way = w;
        }
    }

    // 4) Insert into victim
    cache_line_t *victim = &set->lines[victim_way];
    victim->valid = 1;
    victim->tag   = tag;
    victim->lru   = 0; // mark “most recent”
    
    for (int w = 0; w < c->ways; w++) {
    if (w == victim_way) {
        set->lines[w].valid = 1;
        set->lines[w].tag   = tag;
        set->lines[w].lru   = 0;  // youngest = most recently used
    } else if (set->lines[w].valid) {
        set->lines[w].lru++;     // older by 1
    }
}

    return 0; // “we just inserted => it was a miss”

}


int cache_check_hit_only(cache_t *c, uint64_t addr)
{
    // Extract index and tag from 'addr'
    uint64_t index = (addr >> c->offset_bits) & ((1ULL << c->index_bits) - 1ULL);
    uint64_t tag   = addr >> (c->offset_bits + c->index_bits);

    cache_set_t *set = &c->set[index];

    // Search ways
    for (int w = 0; w < c->ways; w++) {
        cache_line_t *line = &set->lines[w];
        if (line->valid && line->tag == tag) {
            // HIT: update line->lru or your “most‐recent” scheme
            // Possibly line->lru = 0, increment others, etc.
            return 1; // HIT
        }
    }

    return 0; // MISS, no insertion done
}


#ifndef _BP_H_
#define _BP_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    /* gshare */
    int ghr_bits;
    uint32_t ghr;
    uint8_t *pht; /* size 2^ghr_bits */

    /* BTB */
    int btb_size;
    int btb_bits;
    uint64_t *btb_tag;
    uint64_t *btb_dest;
    uint8_t *btb_valid;
    uint8_t *btb_cond;
} bp_t;

extern uint64_t bp_predict_pc; // (if we need to store globally)

void bp_predict(bp_t *bp, uint64_t current_pc);
void bp_update(bp_t *bp, uint64_t branch_pc, int is_conditional, int branch_taken, uint64_t actual_target);

#endif


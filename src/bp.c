#include "bp.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

uint64_t bp_predict_pc = 0; // (if we need to store globally)


void bp_predict(bp_t *bp, uint64_t current_pc)
{
  // 1) btb index
    uint64_t idx = (current_pc >> 2) & ((1 << bp->btb_bits) - 1);

    // 2) check BTB
    if (!bp->btb_valid[idx] || (bp->btb_tag[idx] != current_pc)) {
        // Miss in BTB
        bp_predict_pc = current_pc + 4;
    } else {
        // BTB hit
        if (bp->btb_cond[idx] == 0) {
            // Unconditional branch
            bp_predict_pc = bp->btb_dest[idx];
        } else {
            // Conditional branch → use Gshare
            // XOR the GHR with bits [9:2] of the PC (for 8-bit GHR)
            uint64_t ghr_mask = (1 << bp->ghr_bits) - 1; // e.g. 0xFF
            uint64_t pc_index = (current_pc >> 2) & ghr_mask;
            uint64_t pht_index = pc_index ^ (bp->ghr & ghr_mask);

            // saturating counter in pht[pht_index], 2 bits in a 0..3 range
            uint8_t counter = bp->pht[pht_index] & 0x3;
            if (counter >= 2) {
                // predict taken
                bp_predict_pc = bp->btb_dest[idx];
            } else {
                // predict not-taken
                bp_predict_pc = current_pc + 4;
            }
        }
    }
}

void bp_update(bp_t *bp, uint64_t branch_pc, int is_conditional,int branch_taken, uint64_t actual_target)
{
    // 1) Update the BTB
    uint64_t idx = (branch_pc >> 2) & ((1 << bp->btb_bits) - 1);
    //Because each instruction is 4 bytes, the lowest 2 bits of the PC are always zero for 32-bit alignment. 
    //Shifting right by 2 discards those 2 bits. So idx becomes the BTB index for this branch’s PC

    // Always install/overwrite the entry for this PC
    bp->btb_tag[idx] = branch_pc;
    bp->btb_dest[idx] = actual_target;
    bp->btb_valid[idx] = 1;
    bp->btb_cond[idx] = is_conditional;
    //printf("Condition: %d)\n", is_conditional);
    // 2) If conditional, update PHT + GHR
    if (is_conditional) {
        // compute gshare index
        uint64_t ghr_mask = (1 << bp->ghr_bits) - 1; //This mask keeps the GHR (and PC bits) to exactly ghr_bits
        uint64_t pc_index = (branch_pc >> 2) & ghr_mask; //& ghr_mask picks out the next ghr_bits bits of (branch_pc >> 2).
        // That’s the partial PC we’ll use for gshare
        uint64_t pht_index = pc_index ^ (bp->ghr & ghr_mask); //standard gshare formula: the PHT index is the XOR of (PC bits)
        // and (global history register bits)

        // read old saturating counter
        uint8_t old_counter = bp->pht[pht_index] & 0x3; // in 0..3

        // update saturating counter
        if (branch_taken) {
            if (old_counter < 3) {
                old_counter++;
            }
        } else {
            if (old_counter > 0) {
                old_counter--;
            }
        }
        bp->pht[pht_index] = old_counter;

        // 3) update GHR
        // shift left, push in new bit in LSB
        //   if we define LSB = most recent:
        bp->ghr <<= 1;        
        bp->ghr &= ghr_mask;   // keep it 8 bits
        if (branch_taken) {
            bp->ghr |= 1;      // set LSB --> 1 if newest branch taken 0 if not
        }
    }
    // unconditional branch => do not modify GHR or PHT
}

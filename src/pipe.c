#include "pipe.h"
#include "shell.h"
#include "bp.h"
#include "cache.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

typedef struct {
    char ifIdType[10]; 
    char idExType[10];
    char exMemType[10];
    char memWbType[10];
} InstrStr;
bool stage_prints = false;
bool refsim_prints_comp_on = false;
bool p_finder = false;
int cycle_count = 0;
bool doStall;
bool cacheStall; 
bool flush = false; 
uint64_t prior_pc;
uint32_t mainInstr;
uint16_t mainR;
uint16_t mainD;
uint16_t mainB;
uint16_t mainI;
uint16_t mainCB;
uint16_t mainHLT;
uint16_t mainIW;
uint16_t mainCode;
uint8_t sfBit;
int leftCycles = 10000;
int64_t valA;
int64_t valB;
int icache_stall_counter = 0; 
uint64_t icache_pending_addr = 0;
int icache_cancel = false; 
bool icache_miss_inflight;
bool dcache_miss_inflight;
uint64_t dcache_pending_addr;
int dcache_stall_counter;
bool istall = false; 
bool lastCycle = false; 
bool stallwb = false; 
bool stallfetch = false;
bool first_hit = false; 

bool exHazRn;
bool exHazRm;
bool memHazRn;
bool memHazRm;

cache_t *Icache = NULL;
cache_t *Dcache = NULL;


typedef struct {
    uint32_t instr;
    uint64_t incPC;
    uint64_t valRm;
    uint64_t valRn;
    uint64_t pcIn;
    uint64_t predictedPC; 
} IF_ID;

IF_ID regIFID;

typedef struct {
    uint64_t RmVal;
    uint64_t ShVal;
    uint64_t RnVal;
    uint64_t RdVal;
    uint64_t incPCval;
    int reg2Loc;
    uint64_t immVal16;
    uint64_t immValALU;
    uint8_t sizeBits;
    uint64_t immVal9;
    uint64_t regT;
    uint64_t immVal26;
    int64_t immVal19;
    uint64_t pipePC;
    uint64_t predictedPC; 
    uint8_t condVal;
} ID_EX;

ID_EX regIDEX;

typedef struct {
    int64_t calcRes;
    uint64_t storeRd;
    int regW;
    int64_t sizeStore;
    int64_t effAddr;
    uint64_t storeRt;
    int64_t nextPC;
    uint64_t curPC;
     // we store the "candidate" flags here:
    int newZ; 
    int newN;
} EX_MEM;

EX_MEM regEXMEM;

typedef struct {
    int64_t outRes;
    uint64_t outRd;
    int outRegWrite;
    uint64_t outRt;
    int64_t outPC;
    uint64_t pipePC;
    // fields to carry flags into WB:
    int outZ;
    int outN;
} MEM_WB;

MEM_WB regMEMWB;

int pause;
int RLIST[10]    = {0x458, 0x650, 0x558, 0x450, 0x6B0, 0x658, 0x750, 0x550, 0x758, 0x4D8};//10
int ILIST[5]    = {0x244, 0x34D, 0x344, 0x3C4, 0x2C4};//5
int CBLIST[3]  = {0xB5, 0xB4, 0x54}; //3
int DLIST[6]    = {0x7C2, 0x1C2, 0x3C2, 0x7C0, 0x1C0, 0x3C0}; //6 total
int HLIST[1]  = {0x6a2};
int BLIST[1]    = {0x5};
int IWLIST[1]  = {0x694};

InstrStr stageType;

Pipe_State pipe;     
Pipe_Op pipe_op;     
int RUN_BIT;

bool inList(int arrayV[], int l, int val) {
    for(int i=0; i<l; i++){
        if(arrayV[i] == val) {
           //printf("Found value 0x%x in list\n", val);
            return true;
        }
    }
    return false;
}

void forward_A_in_exec(){
    if (exHazRn) {
        // EX->EX
        valA = regEXMEM.calcRes;
    } 
    else if (memHazRn) {
        // MEM->EX
        valA = regMEMWB.outRes;
    }
    else {
        // No forwarding
        valA = pipe.REGS[regIDEX.RnVal];
    }
}

void forward_B_in_exec(){
    if (exHazRm) {
        // EX->EX
        valB = regEXMEM.calcRes;
    } 
    else if (memHazRm) {
        // MEM->EX
        valB = regMEMWB.outRes;
    }
    else {
        // No forwarding
        valB = pipe.REGS[regIDEX.RmVal];
    }
}

const char* get_instruction_name(uint16_t opcode, uint64_t rd) {
    if(opcode == 0x458) return "ADD";
    if(opcode == 0x558) return "ADDS";
    if(opcode == 0x450) return "AND";
    if(opcode == 0x750) return "ANDS";
    if(opcode == 0x650) return "EOR";
    if(opcode == 0x550) return "ORR";
    if(opcode == 0x658) return "SUB";
    if(opcode == 0x758) return (rd == 31) ? "CMP" : "SUBS";
    if(opcode == 0x4D8) return "MUL";
    if(opcode == 0x6B0) return "BR";
    if(opcode == 0x244) return "ADDI";
    if(opcode == 0x34D) return "LSL/LSR";
    if(opcode == 0x344) return "SUBI";
    if(opcode == 0x3C4) return (rd == 31) ? "CMPI" : "SUBIS";
    if(opcode == 0x2C4) return "ADDIS";
    if(opcode == 0x7C2) return "LDUR";
    if(opcode == 0x1C2) return "LDURB";
    if(opcode == 0x3C2) return "LDURH";
    if(opcode == 0x7C0) return "STUR";
    if(opcode == 0x1C0) return "STURB";
    if(opcode == 0x3C0) return "STURH";
    if(opcode == 0x5) return "B";
    if(opcode == 0xB5) return "CBNZ";
    if(opcode == 0xB4) return "CBZ";
    if(opcode == 0x54) return "B.cond";
    if(opcode == 0x694) return "MOVZ";
    if(opcode == 0x6a2) return "HLT";
    return "Unknown";
}

void pipe_init()
{
    memset(&pipe, 0, sizeof(Pipe_State));
    pipe.PC = 0x00400000;
	  RUN_BIT = true;

    Icache = cache_new(64, 4, 32);   // 64 sets, 4 ways, 32 bytes/line
    Dcache = cache_new(256, 8, 32);  // 256 sets, 8 ways, 32 bytes/line

    // Allocate & initialize the branch predictor:
    pipe.bp = (bp_t*) malloc(sizeof(bp_t));
    memset(pipe.bp, 0, sizeof(bp_t));

    //initialization
    pipe.bp->ghr_bits = 8;   // 8-bit GHR
    pipe.bp->btb_bits = 10;  // 10 bits for 1024 BTB entries
    pipe.bp->btb_size = (1 << pipe.bp->btb_bits);

    // Allocate arrays for the PHT and BTB:
    pipe.bp->pht = (uint8_t*) malloc( (1 << pipe.bp->ghr_bits) * sizeof(uint8_t) );
    memset(pipe.bp->pht, 0, (1 << pipe.bp->ghr_bits) * sizeof(uint8_t));

    pipe.bp->btb_tag   = (uint64_t*) malloc(pipe.bp->btb_size * sizeof(uint64_t));
    pipe.bp->btb_dest  = (uint64_t*) malloc(pipe.bp->btb_size * sizeof(uint64_t));
    pipe.bp->btb_valid = (uint8_t*)  malloc(pipe.bp->btb_size * sizeof(uint8_t));
    pipe.bp->btb_cond  = (uint8_t*)  malloc(pipe.bp->btb_size * sizeof(uint8_t));

    // Initialize BTB arrays
    memset(pipe.bp->btb_tag,   0, pipe.bp->btb_size * sizeof(uint64_t));
    memset(pipe.bp->btb_dest,  0, pipe.bp->btb_size * sizeof(uint64_t));
    memset(pipe.bp->btb_valid, 0, pipe.bp->btb_size * sizeof(uint8_t));
    memset(pipe.bp->btb_cond,  0, pipe.bp->btb_size * sizeof(uint8_t));
}

void pipe_cycle()
{
	pipe_stage_wb();
	pipe_stage_mem();
	pipe_stage_execute();
	pipe_stage_decode();
	pipe_stage_fetch();
}


void pipe_stage_wb()
{
    cycle_count += 1;
    if(stage_prints){
        printf("\n\n\n00000000000000000000000000000[ cycle %d ]000000000000000000000000000000\n", cycle_count);
        printf("\n\n```````````````````````````````` WB STAGE ````````````````````````````````\n");
        printf("\nWB PC: 0x%lx\n", regMEMWB.pipePC);
    }
    
    if(leftCycles == 0) {
        stat_inst_retire += 1;
        return;
    }
    
    if(dcache_miss_inflight && !lastCycle){
        return;
    }
    if(pipe_op.MEMtoWB_Op == 0) {
       //printf("NOP instruction - no writeback needed\n");
        return;
    }

    stat_inst_retire += 1;
    if(regMEMWB.outRd != 31) {
       //printf("Processing writeback for instruction 0x%x\n", pipe_op.MEMtoWB_Op);
        if(inList(RLIST,10,pipe_op.MEMtoWB_Op)){
            pipe.REGS[regMEMWB.outRd] = regMEMWB.outRes;
           //printf("R-type: Writing 0x%lx to register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
        }
        if(inList(ILIST,5,pipe_op.MEMtoWB_Op)){
            pipe.REGS[regMEMWB.outRd] = regMEMWB.outRes;
           //printf("I-type: Writing 0x%lx to register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
        } else if(inList(DLIST,6,pipe_op.MEMtoWB_Op)){
            if(regMEMWB.outRes != 100000){
                //printf("outres = 0x%lx\n", regMEMWB.outRes);
                pipe.REGS[regMEMWB.outRt] = regMEMWB.outRes;
               //printf("D-type: Writing 0x%lx to register %ld\n", regMEMWB.outRes, regMEMWB.outRt);
            } else {
               //printf("D-type: No writeback needed (store instruction)\n");
            }
        } else if(inList(IWLIST,1,pipe_op.MEMtoWB_Op)){
            pipe.REGS[regMEMWB.outRd] = regMEMWB.outRes;
           //printf("IW-type: Writing 0x%lx to register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
        }
    }
    if (pipe_op.MEMtoWB_Op == 0x558 || pipe_op.MEMtoWB_Op == 0x2C4 || pipe_op.MEMtoWB_Op == 0x750 || pipe_op.MEMtoWB_Op == 0x758 
    || pipe_op.MEMtoWB_Op == 0x3C4) {
        pipe.FLAG_Z = regMEMWB.outZ;
        pipe.FLAG_N = regMEMWB.outN;
    }
}



void pipe_stage_mem()
{
    if(stage_prints){
        printf("\n\n```````````````````````````````` MEM STAGE ````````````````````````````````\n");
        printf("\nMEM PC: 0x%lx\n", regEXMEM.curPC);
    }

    if(leftCycles < 2) {
        //printf("Skipping MEM stage (cycles remaining < 2)\n");
        return;
    }

    if (dcache_miss_inflight) {
      //printf("dcache miss in flight\n");
        dcache_stall_counter--;
        if (dcache_stall_counter == 0) {
            stat_inst_retire += 1;
            cache_update(Dcache, dcache_pending_addr);
            if(refsim_prints_comp_on){printf("dcache fill at cycle %d\n", cycle_count);}
                lastCycle = true; 
                stallwb = true; 
            
        } else{
            if(refsim_prints_comp_on){printf("dcache stall (%d)\n", dcache_stall_counter);}
            return;
        }
    }

    if(pipe_op.EXtoMEM_Op == 0) {
        pipe_op.MEMtoWB_Op = pipe_op.EXtoMEM_Op;
        //printf("NOP instruction - passing through MEM stage\n");
        return;
    }

    //printf("Processing instruction 0x%x in MEM stage\n", pipe_op.EXtoMEM_Op);
    pipe_op.MEMtoWB_Op = pipe_op.EXtoMEM_Op;
    regMEMWB.outZ = regEXMEM.newZ;
    regMEMWB.outN = regEXMEM.newN;
    regMEMWB.pipePC =  regEXMEM.curPC;

    if(inList(RLIST,10,pipe_op.EXtoMEM_Op)) {
        //printf("Processing R-type instruction\n");
        strcpy(stageType.memWbType,"R");
        //printf("Set memWbType to R\n");
        if(pipe_op.EXtoMEM_Op != 0x758) {
            regMEMWB.outRes = regEXMEM.calcRes;
            regMEMWB.outRd = regEXMEM.storeRd;
            //printf("Passing result 0x%lx to WB stage for register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
        } else {
            regMEMWB.outRes = regEXMEM.calcRes;
            regMEMWB.outRd = regEXMEM.storeRd;
            //printf("Passing result 0x%lx to WB stage for register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
        }
    } else if(inList(ILIST,5,pipe_op.EXtoMEM_Op)) {
        //printf("Processing I-type instruction\n");
        strcpy(stageType.memWbType,"I");
        //printf("Set memWbType to I\n");
        if(pipe_op.EXtoMEM_Op == 0x3C4 && regEXMEM.storeRd == 31) {
        }
        regMEMWB.outRes = regEXMEM.calcRes;
        regMEMWB.outRd = regEXMEM.storeRd;
        //printf("Passing result 0x%lx to WB stage for register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
    }
    else if(inList(DLIST,6,pipe_op.EXtoMEM_Op)) {
        //printf("Processing D-type instruction\n");
        strcpy(stageType.memWbType,"D");
        //printf("Set memWbType to D\n");
        uint64_t effAddr = regEXMEM.effAddr;
        int hit = cache_check_hit_only(Dcache, effAddr);

        if (!hit) {
            // MISS => begin a 50‐cycle stall
            if(refsim_prints_comp_on){printf("dcache miss (0x%lx) at cycle %d\n", effAddr, cycle_count);}
            dcache_miss_inflight = true;
            dcache_stall_counter = 50;
            dcache_pending_addr = effAddr; // Block align
            return; // Don’t do normal load/store logic now
        }
        else {
            // HIT => do load/store in 1 cycle
            if(refsim_prints_comp_on){printf("dcache hit (0x%lx) at cycle %d\n", effAddr, cycle_count);}
            if(pipe_op.EXtoMEM_Op == 0x7C2) {
            //printf("Processing LDUR instruction\n");
            if(regEXMEM.sizeStore == 2) {
                regMEMWB.outRes = mem_read_32(regEXMEM.effAddr);
                regMEMWB.outRt = regEXMEM.storeRt;
                //printf("32-bit load: Read 0x%lx from address 0x%lx\n", regMEMWB.outRes, regEXMEM.effAddr);
            } else if(regEXMEM.sizeStore == 3){
                uint64_t lw = mem_read_32(regEXMEM.effAddr);
                uint64_t uw = ((uint64_t)mem_read_32(regEXMEM.effAddr+4))<<32;
                regMEMWB.outRes = (uw|lw);
                regMEMWB.outRt = regEXMEM.storeRt;
                //printf("64-bit load: Read 0x%lx from addresses 0x%lx and 0x%lx\n", regMEMWB.outRes, regEXMEM.effAddr, regEXMEM.effAddr+4);
            }
        } else if(pipe_op.EXtoMEM_Op == 0x1C2){
            //printf("Processing LDURB instruction\n");
            regMEMWB.outRes = (uint8_t) mem_read_32(regEXMEM.effAddr);
            regMEMWB.outRt = regEXMEM.storeRt;
            //printf("Byte load: Read 0x%lx from address 0x%lx\n", regMEMWB.outRes, regEXMEM.effAddr);
        } else if(pipe_op.EXtoMEM_Op == 0x3C2){
            //printf("Processing LDURH instruction\n");
            regMEMWB.outRes = (mem_read_32(regEXMEM.effAddr) >> 16);
            regMEMWB.outRt = regEXMEM.storeRt;
            //printf("Halfword load: Read 0x%lx from address 0x%lx\n", regMEMWB.outRes, regEXMEM.effAddr);
        } else if(pipe_op.EXtoMEM_Op == 0x7C0){
            //printf("Processing STUR instruction\n");
            if(regEXMEM.sizeStore == 2){
                mem_write_32(regEXMEM.effAddr, pipe.REGS[regEXMEM.storeRt]);
                regMEMWB.outRt = 100000;
                regMEMWB.outRes = 100000;
                //printf("32-bit store: Wrote 0x%lx to address 0x%lx\n", pipe.REGS[regEXMEM.storeRt], regEXMEM.effAddr);
            } else if(regEXMEM.sizeStore == 3){
                uint64_t valX = pipe.REGS[regEXMEM.storeRt];
                uint32_t lwX = (uint32_t)(valX & 0xFFFFFFFF);
                uint32_t uwX = (uint32_t)((valX >> 32)&0xFFFFFFFF);
                mem_write_32(regEXMEM.effAddr,lwX);
                mem_write_32(regEXMEM.effAddr+4,uwX);
                regMEMWB.outRt = 100000;
                regMEMWB.outRes = 100000;
                //printf("64-bit store: Wrote 0x%x and 0x%x to addresses 0x%lx and 0x%lx\n", lwX, uwX, regEXMEM.effAddr, regEXMEM.effAddr+4);
            }
        } else if(pipe_op.EXtoMEM_Op == 0x1C0){
            //printf("Processing STURB instruction\n");
            uint8_t valB = pipe.REGS[regEXMEM.storeRt];
            mem_write_32(regEXMEM.effAddr,valB);
            regMEMWB.outRt = 100000;
            regMEMWB.outRes = 100000;
            //printf("Byte store: Wrote 0x%x to address 0x%lx\n", valB, regEXMEM.effAddr);
        }
      }
    } else if(inList(IWLIST,1,pipe_op.EXtoMEM_Op)) {
        //printf("Processing IW-type instruction\n");
        strcpy(stageType.memWbType,"IW");
        regMEMWB.outRes = regEXMEM.calcRes;
        regMEMWB.outRd = regEXMEM.storeRd;
        //printf("Passing result 0x%lx to WB stage for register %ld\n", regMEMWB.outRes, regMEMWB.outRd);
    }


}

void pipe_stage_execute()
{
    if(stage_prints){
        printf("\n\n```````````````````````````````` EX STAGE ````````````````````````````````\n");
        printf("\nEX PC: 0x%lx\n",regIDEX.pipePC);
    }
   if(leftCycles < 3) {
       //printf("Skipping EX stage (cycles remaining < 4)\n");
        return;
    }
    
    

    if(dcache_miss_inflight && !lastCycle){
      return;
    }

    if(pipe_op.IDtoEX_Op == 0){
        pipe_op.EXtoMEM_Op = pipe_op.IDtoEX_Op;
       //printf("NOP instruction - passing through EX stage\n");
        return;
    }

    pipe_op.EXtoMEM_Op = pipe_op.IDtoEX_Op;


    if(doStall){
      //printf("do stall = %d", doStall);
       pipe_op.EXtoMEM_Op = 0;
       //printf("Stalling due to hazard detection\n");
       doStall = false; 
        return;
    }

    regEXMEM.curPC = regIDEX.pipePC;
    //printf("Updated regEXMEM.curPC to 0x%lx\n", regEXMEM.curPC);

    if(inList(RLIST,10,pipe_op.IDtoEX_Op)) {
       //printf("Processing R-type instruction\n");
        strcpy(stageType.exMemType,"R");
        forward_A_in_exec();
        forward_B_in_exec();
        //printf("Set exMemType to R\n");
        if(pipe_op.IDtoEX_Op == 0x458){
           //printf("Processing ADD instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA + valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("ADD: %lx + %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x558){
           //printf("Processing ADDS instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA + valB;
            regEXMEM.storeRd = regIDEX.RdVal;
            regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
            regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
           //printf("ADDS: %lx + %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x450){
           //printf("Processing AND instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA & valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("AND: %lx & %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x750){
           //printf("Processing ANDS instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA & valB;
            regEXMEM.storeRd = regIDEX.RdVal;
            regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
            regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
           //printf("ANDS: %lx & %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x650){
           //printf("Processing EOR instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA ^ valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("EOR: %lx ^ %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x550){
           //printf("Processing ORR instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA | valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("ORR: %lx | %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x658){
           //printf("Processing SUB instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA - valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("SUB: %lx - %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x758){
            regEXMEM.calcRes = valA - valB;
            if(regIDEX.RdVal != 31){
               //printf("Processing SUBS instruction\n");
                regEXMEM.regW = 1;
                regEXMEM.storeRd = regIDEX.RdVal;
               //printf("SUBS: %lx - %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
            } else {
               //printf("Processing CMP instruction\n");
                regEXMEM.regW = 0;
                regEXMEM.storeRd = 31; 

               //printf("CMP: %lx - %lx = %lx (flags only)\n", valA, valB, regEXMEM.calcRes);
            }
            regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
            regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
           //printf("Pipe flags: (Z=%d, N=%d)\n",regEXMEM.newZ, regEXMEM.newN );
        } else if(pipe_op.IDtoEX_Op == 0x4D8){
           //printf("Processing MUL instruction\n");
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA * valB;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("MUL: %lx * %lx = %lx, Destination=R%ld\n", valA, valB, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x6B0){
           //printf("Processing BR instruction\n");
            pipe.PC = valA;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("BR: Jumping to address 0x%lx\n", pipe.PC);
        }
    } else if(inList(ILIST,5,pipe_op.IDtoEX_Op)) {
       //printf("Processing I-type instruction\n");
        strcpy(stageType.exMemType,"I");
       //printf("Set exMemType to I\n");
        if(pipe_op.IDtoEX_Op == 0x244){
           //printf("Processing ADDI instruction\n");
            forward_A_in_exec();
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA + regIDEX.immValALU;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("ADDI: %lx + %lx = %lx, Destination=R%ld\n", valA, regIDEX.immValALU, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x34D){
           //printf("Processing LSL/LSR instruction\n");
            uint16_t sft = regIDEX.immValALU & 0x3F;
            regEXMEM.regW = 1;
            if(sft == 63){
                forward_A_in_exec();
                regEXMEM.calcRes = (valA >> ((regIDEX.immValALU>>6)&0x3F));
                regEXMEM.storeRd = regIDEX.RdVal;
               //printf("LSR: %lx >> %lu = %lx, Destination=R%ld\n", valA, ((regIDEX.immValALU>>6)&0x3F), regEXMEM.calcRes, regEXMEM.storeRd);
            } else {
                forward_A_in_exec();
                uint16_t negS = -((regIDEX.immValALU>>6)&0x3F)%64;
                regEXMEM.calcRes = valA << (negS);
                regEXMEM.storeRd = regIDEX.RdVal;
               //printf("LSL: %lx << %d = %lx, Destination=R%ld\n", valA, negS, regEXMEM.calcRes, regEXMEM.storeRd);
            }
        } else if(pipe_op.IDtoEX_Op == 0x344){
           //printf("Processing SUBI instruction\n");
            forward_A_in_exec();
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA - regIDEX.immValALU;
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("SUBI: %lx - %lx = %lx, Destination=R%ld\n", valA, regIDEX.immValALU, regEXMEM.calcRes, regEXMEM.storeRd);
        } else if(pipe_op.IDtoEX_Op == 0x3C4){
            forward_A_in_exec();
            if(regIDEX.RdVal != 31){
               //printf("Processing SUBIS instruction\n");
                regEXMEM.regW = 1;
                regEXMEM.calcRes = valA - regIDEX.immValALU;
                regEXMEM.storeRd = regIDEX.RdVal;
                regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
                regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
               //printf("SUBIS: %lx - %lx = %lx, Destination=R%ld\n", valA, regIDEX.immValALU, regEXMEM.calcRes, regEXMEM.storeRd);
            } else {
               //printf("Processing CMPI instruction\n");
                //holdFlags = true;
                regEXMEM.regW = 0;
                regEXMEM.calcRes = valA - regIDEX.immValALU;
                regEXMEM.storeRd = 31;    // or keep as regIDEX.RdVal
                // set flags here too
                regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
                regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
               //printf("CMPI: %lx - %lx = %lx (flags only)\n", pipe.REGS[regIDEX.RnVal], regIDEX.immValALU, regEXMEM.calcRes);
            }
        } else if(pipe_op.IDtoEX_Op == 0x2C4){
           //printf("Processing ADDIS instruction\n");
            forward_A_in_exec();
            regEXMEM.regW = 1;
            regEXMEM.calcRes = valA + regIDEX.immValALU;
            regEXMEM.storeRd = regIDEX.RdVal;
            regEXMEM.newZ  = (regEXMEM.calcRes == 0) ? 1 : 0;
            regEXMEM.newN = ((regEXMEM.calcRes >> 63) & 1) ? 1 : 0;
           //printf("ADDIS: %lx + %lx = %lx, Destination=R%ld\n", valA, regIDEX.immValALU, regEXMEM.calcRes, regEXMEM.storeRd);
        }
    } else if(inList(DLIST,6,pipe_op.IDtoEX_Op)) {
       //printf("Processing D-type instruction\n");
        strcpy(stageType.exMemType,"D");
       //printf("Set exMemType to D\n");
        if(pipe_op.IDtoEX_Op == 0x7C2 || pipe_op.IDtoEX_Op == 0x1C2 || pipe_op.IDtoEX_Op == 0x3C2) {
            regEXMEM.regW = 1;
            // execForward("D");
            forward_A_in_exec();
            regEXMEM.effAddr = valA + regIDEX.immVal9;
            regEXMEM.storeRt = regIDEX.regT;
            regEXMEM.sizeStore = regIDEX.sizeBits;
            // Added check: if a dcache miss is already in flight for the same cache block, stall the load
            if(dcache_miss_inflight && (regEXMEM.effAddr >= dcache_pending_addr) && (regEXMEM.effAddr < dcache_pending_addr + 32)) {
                pipe_op.EXtoMEM_Op = 0;
                doStall = true;
                return;
            }
           //printf("Load: Calculating effective address 0x%lx + %ld = 0x%lx\n", valA, regIDEX.immVal9, regEXMEM.effAddr);
        } else {
            uint64_t base = 0; 
            if ((regEXMEM.storeRd == regIDEX.RnVal) && (regEXMEM.storeRd  == 1)) {
                base = regEXMEM.calcRes;
            } else if ((regMEMWB.outRd== regIDEX.RnVal) && (regMEMWB.outRegWrite == 1)) {
               base = regMEMWB.outRes;
            } else {
                base = pipe.REGS[regIDEX.RnVal];
            }
            //effAddr = baseVal + offset;
            regEXMEM.regW = 0;
            regEXMEM.effAddr = base + (int32_t)regIDEX.immVal9;
            // regEXMEM.regW = 0;
            // regEXMEM.effAddr = pipe.REGS[regIDEX.RnVal] + regIDEX.immVal9;
            regEXMEM.storeRt = regIDEX.regT;
            regEXMEM.sizeStore = regIDEX.sizeBits;
           //printf("Store: Calculating effective address 0x%lx + %ld = 0x%lx\n", pipe.REGS[regIDEX.RnVal], regIDEX.immVal9, regEXMEM.effAddr);
        }
    } else if(inList(BLIST,1,pipe_op.IDtoEX_Op)) {
       //printf("Processing B-type instruction\n");
        strcpy(stageType.exMemType,"B");
       //printf("Set exMemType to B\n");
        int offsetVal = (int32_t)regIDEX.immVal26 * 4;
        uint64_t actual_target = regIDEX.pipePC + offsetVal;
        // tookBranch = (actual_target != (regIDEX.pipePC + 4)); 
        bool tookBranch = true;
        bp_update(pipe.bp,regIDEX.pipePC,0, tookBranch, actual_target);
        bool mispredicted = (regIDEX.predictedPC != actual_target);
        if (mispredicted) {
            //flush instructions in IF, ID, EX pipeline registers:
            regIFID.instr = 0;
            memset(&regIDEX,  0, sizeof(ID_EX));
            memset(&regEXMEM, 0, sizeof(EX_MEM));

            // fix the PC to the actual target
           //printf("PC from 0x%lX to 0x%lX in execute (B) \n", pipe.PC, actual_target);
            pipe.PC = actual_target;
            stat_squash++;
            flush = true; 
            if (icache_miss_inflight) {
                // Check if actual target falls outside the pending icache block (32 bytes)
                if (actual_target < icache_pending_addr || actual_target >= (icache_pending_addr + 32)) {
                    icache_cancel = true;
                } else {
                    icache_cancel = false;
                    icache_miss_inflight = false;
                    icache_pending_addr = 0;
                    cacheStall = false;
                    icache_stall_counter = 0;
                }
                //printf("ICache cancel = %d \n", icache_cancel);
            }
        }
    } else if(inList(CBLIST,3,pipe_op.IDtoEX_Op)) {
       //printf("Processing CB-type instruction\n");
        bool tookBranch = false;
        uint64_t actual_target = regIDEX.pipePC + 4;
        if(pipe_op.IDtoEX_Op == 0xB5){
           //printf("Processing CBNZ instruction\n");
            if(pipe.REGS[regIDEX.regT]!=0) {
                tookBranch = true; 
                int off = (int32_t)regIDEX.immVal19 * 4;
                actual_target = regIDEX.pipePC + off;
            }
        } else if(pipe_op.IDtoEX_Op == 0xB4){
           //printf("Processing CBZ instruction\n");
            if(pipe.REGS[regIDEX.regT]==0) {
                tookBranch = true; 
                int off2 = (int32_t)regIDEX.immVal19 * 4;
                actual_target = regIDEX.pipePC + off2;
            }
        } else if(pipe_op.IDtoEX_Op == 0x54){
            uint8_t cCheck = regIDEX.condVal;
           //printf("Processing B.cond instruction (condition code: %d)\n", cCheck);
            bool res = false;
            if(cCheck == 0)      { if(regEXMEM.newZ == 1) res = true; }
            else if(cCheck == 1) { if(regEXMEM.newZ== 0) res = true; }
            else if(cCheck == 10){ if(regEXMEM.newN == 0) res = true; }
            else if(cCheck == 11){ if(regEXMEM.newN== 1) res = true; }
            else if(cCheck == 12){ if(regEXMEM.newZ== 0 && regEXMEM.newN== 0) res = true; }
            else if(cCheck == 13){ if(regEXMEM.newZ == 1 || regEXMEM.newN == 1) res = true; }
           //printf("Condition check result: %s (Z=%d, N=%d)\n", res ? "true" : "false", regEXMEM.newZ, regEXMEM.newN);
           //printf("Pipe Flags:Z=%d, N=%d\n", pipe.FLAG_Z, pipe.FLAG_N);
            if(res){
              tookBranch = true; 
              int off3 = (int32_t)regIDEX.immVal19 * 4;
              actual_target = regIDEX.pipePC + off3;
              //printf("RegIDEX PC, off3, Actual target: 0x%lX, %d ,0x%lX\n", regIDEX.pipePC, off3, actual_target);
            }
        }
        bp_update(pipe.bp, regIDEX.pipePC, 1, tookBranch, actual_target);
       //printf("Took Branch: %s (Z=%d, N=%d)\n");

        // We stored the predicted PC in regIDEX.predictedPC, set in the fetch stage
        uint64_t predicted_pc = regIDEX.predictedPC;
        //printf("Predicted PC: 0x%lX \n", predicted_pc);

        // The correct "actual PC" is either actual_target (if taken) or pcIn+4 (if not)
        uint64_t correct_pc = (tookBranch) ? actual_target : (regIDEX.pipePC + 4);
       //printf("Correct PC: 0x%lX \n", correct_pc);

        bool mispredicted = (predicted_pc != correct_pc);

        if (mispredicted) {
            // flush pipeline registers: IF/ID, ID/EX, EX/MEM
            regIFID.instr = 0;
            memset(&regIDEX,  0, sizeof(ID_EX));
            memset(&regEXMEM, 0, sizeof(EX_MEM));

            // correct the fetch-stage PC
           //printf("PC from 0x%lX to 0x%lX in execute (CB) \n", pipe.PC, correct_pc);
            pipe.PC = correct_pc;
            stat_squash++;
            flush = true;
           //printf("flush = %d \n", flush);
            if (icache_miss_inflight) {
                if (! ((actual_target >= icache_pending_addr) && (actual_target <= icache_pending_addr + 32) )) {
                    icache_cancel = true;
                    //printf("actual_target: 0x%lx, icache_pending_addr: 0x%lx\n", actual_target, icache_pending_addr);
                } else {
                    icache_cancel = false;
                    flush = false;
                }
                //printf("ICache cancel = %d \n", icache_cancel);
            }
        }
    } else if(inList(IWLIST,1,pipe_op.IDtoEX_Op)){
       //printf("Processing IW-type instruction\n");
        if(pipe_op.IDtoEX_Op == 0x694){
            regEXMEM.regW = 1;
            regEXMEM.calcRes = (regIDEX.immVal16 << 0);
            regEXMEM.storeRd = regIDEX.RdVal;
           //printf("MOV immediate: Result=0x%lx, Destination=R%ld\n", regEXMEM.calcRes, regEXMEM.storeRd);
        }
        strcpy(stageType.exMemType,"IW");
       //printf("Set exMemType to IW\n");
    }

    if(!doStall) {
        pipe_op.EXtoMEM_Op = pipe_op.IDtoEX_Op;
       //printf("Passing instruction 0x%x to MEM stage\n", pipe_op.EXtoMEM_Op);
    }
}

void pipe_stage_decode()
{
if(stage_prints){
    printf("\n\n```````````````````````````````` ID STAGE ````````````````````````````````\n");
    printf("\nID PC: 0x%lx\n", regIFID.pcIn);
}
    if(leftCycles < 4) {
       //printf("Skipping ID stage (cycles remaining < 4)\n");
        return;
    }
    if(dcache_miss_inflight && !lastCycle){
      //printf("Skipping ID stage (dcache miss cycle)\n");
      return;
    }
    // if(lastCycle){
    //   //printf("last dcache miss cycle");
    //   return;
    // }
    if(doStall) {
       //printf("Stalling ID stage\n");
     //  doStall = false; 
        return;
    }

    // if(istall) {
    //     //printf("Holding ID stage (icache miss)\n");
    //     return;
    // }
    
    
    if(regIFID.instr == 0 || istall) {
        regIDEX.pipePC = regIFID.pcIn;
        pipe_op.IDtoEX_Op = 0;
       //printf("Holding ID stage (branch resolution or NOP)\n");
        return;
    }

   //printf("Processing instruction 0x%x in ID stage\n", regIFID.instr);
    regIDEX.pipePC = regIFID.pcIn;
    regIDEX.predictedPC = regIFID.predictedPC;
   //printf("Updated regIDEX.pipePC to 0x%lx\n", regIDEX.pipePC);

    sfBit = (regIFID.instr >> 31) & 0x1;
    mainR  = (regIFID.instr >> 21) & 0x7FF;
    mainI  = (regIFID.instr >> 22) & 0x3FF;
    mainD  = (regIFID.instr >> 21) & 0x7FF;
    mainB  = (regIFID.instr >> 26) & 0x3F;
    mainCB = (regIFID.instr >> 24) & 0xFF;
    mainIW = (regIFID.instr >> 21) & 0x7FF;
    mainHLT= (regIFID.instr >> 21) & 0x7FF;

   //printf("Decoded fields - sfBit: %d, mainR: 0x%x, mainI: 0x%x, mainD: 0x%x\n", sfBit, mainR, mainI, mainD);
   //printf("mainB: 0x%x, mainCB: 0x%x, mainIW: 0x%x, mainHLT: 0x%x\n", mainB, mainCB, mainIW, mainHLT);

    if(inList(RLIST,10,mainR)){
       //printf("Identified R-type instruction\n");
        regIFID.valRm = (regIFID.instr >> 16)&0xF;
        regIFID.valRn = (regIFID.instr >> 5)&0x1F;
        pipe_op.IDtoEX_Op = mainR;
        regIDEX.RmVal = (regIFID.instr >> 16)&0xF;
        regIDEX.ShVal = (regIFID.instr >> 11)&0x1F;
        regIDEX.RnVal = (regIFID.instr >> 5)&0x1F;
        regIDEX.RdVal = regIFID.instr & 0x1F;
        regIDEX.reg2Loc = 0;
        strcpy(stageType.idExType,"R");
       //printf("R-type fields - Rm: %ld, Rn: %ld, Rd: %ld, Shift: %ld\n",regIDEX.RmVal, regIDEX.RnVal, regIDEX.RdVal, regIDEX.ShVal);
    } else if(inList(ILIST,5,mainI)){
       //printf("Identified I-type instruction\n");
        pipe_op.IDtoEX_Op = mainI;
        regIDEX.RdVal = regIFID.instr & 0x1F;
        regIDEX.RnVal = (regIFID.instr >> 5)&0x1F;
        regIDEX.immValALU = (regIFID.instr >> 10)&0xFFF;
        regIDEX.incPCval = regIFID.incPC;
        strcpy(stageType.idExType,"I");
       //printf("I-type fields - Rn: %ld, Rd: %ld, Immediate: 0x%lx\n",regIDEX.RnVal, regIDEX.RdVal, regIDEX.immValALU);
    } else if(inList(DLIST,6,mainD)){
       //printf("Identified D-type instruction\n");
        pipe_op.IDtoEX_Op = mainD;
        regIDEX.immVal9 = (regIFID.instr >> 12)&0x1FF;
        regIDEX.RnVal = (regIFID.instr >> 5)&0x1F;
        regIDEX.regT = regIFID.instr & 0x1F;
        regIDEX.incPCval = regIFID.incPC;
        regIDEX.sizeBits = (regIFID.instr >> 30)&0x3;
        regIDEX.reg2Loc = 1;
        strcpy(stageType.idExType,"D");
       //printf("D-type fields - Rn: %ld, Rt: %ld, Immediate: %ld, Size: %d\n",regIDEX.RnVal, regIDEX.regT, regIDEX.immVal9, regIDEX.sizeBits);
    } else if(inList(BLIST,1,mainB)){
       //printf("Identified B-type instruction\n");
        pipe_op.IDtoEX_Op = mainB;
        regIDEX.immVal26 = (regIFID.instr)&0x3FFFFFF;
        regIDEX.immVal26 = (int64_t)((regIDEX.immVal26<<38)>>38);
        strcpy(stageType.idExType,"B");
       //printf("B-type fields - Immediate: %ld (0x%lx)\n",(int64_t)regIDEX.immVal26, regIDEX.immVal26);
    } else if(inList(CBLIST,3,mainCB)){
       //printf("Identified CB-type instruction\n");
        pipe_op.IDtoEX_Op = mainCB;
        if(mainCB == 0xB5 || mainCB == 0xB4 ){
          regIDEX.regT = regIFID.instr & 0x1F;
        }else{
          uint8_t cond = (regIFID.instr & 0xF);  // lower nibble
          regIDEX.condVal = cond;
        }
        regIDEX.immVal19 = (regIFID.instr >> 5)& 0x7FFFF;
        regIDEX.immVal19 = (int64_t)((regIDEX.immVal19<<45)>>45);
        regIDEX.reg2Loc = 1;
        strcpy(stageType.idExType,"CB");
       //printf("CB-type fields - Rt: %ld, Immediate: %ld (0x%lx)\n",regIDEX.regT, (int64_t)regIDEX.immVal19, regIDEX.immVal19);
    } else if(inList(IWLIST,1,mainIW)){
       //printf("Identified IW-type instruction\n");
        pipe_op.IDtoEX_Op = mainIW;
        regIDEX.RdVal = regIFID.instr & 0x1F;
        regIDEX.immVal16 = (regIFID.instr >> 5)&0xFFFF;
        regIDEX.incPCval = regIFID.incPC;
        strcpy(stageType.idExType,"IW");
       //printf("IW-type fields - Rd: %ld, Immediate: 0x%lx\n",regIDEX.RdVal, regIDEX.immVal16);
    } else if(inList(HLIST,1,mainHLT)){
       //printf("Identified HLT instruction\n");
        leftCycles = 4;
        //printf("leftCycles = %d", leftCycles );
        return;
    }
    //EX->EX hazard for Rn:
    exHazRn = (regEXMEM.regW && (regEXMEM.storeRd != 31) 
                    && (regEXMEM.storeRd == regIDEX.RnVal));
    // EX->EX hazard for Rm:
    exHazRm = (regEXMEM.regW && (regEXMEM.storeRd != 31) 
                    && (regEXMEM.storeRd == regIDEX.RmVal));

    // MEM->EX (WB->EX) hazard for Rn:
    memHazRn = (regMEMWB.outRegWrite && (regMEMWB.outRd != 31)
                     && (regMEMWB.outRd == regIDEX.RnVal));
    // MEM->EX (WB->EX) hazard for Rm:
    memHazRm = (regMEMWB.outRegWrite && (regMEMWB.outRd != 31)
                     && (regMEMWB.outRd == regIDEX.RmVal));

    regIDEX.incPCval = regIFID.incPC;
   //printf("Updated regIDEX.incPCval to 0x%lx\n", regIDEX.incPCval);
}

void pipe_stage_fetch()
{
    if(stage_prints){
        printf("\n\n`````````````````````````````````` IF STAGE ````````````````````````````````\n");
        printf("PC: 0x%lx\n", pipe.PC);
    }

    if (!RUN_BIT){
      return;
    }
    if(lastCycle){
      //printf("stalling in fetch- from last dcache (icache)\n");
      lastCycle = false; 
      dcache_miss_inflight = false; 
    //  pipe.PC = pipe.PC+4;
     // return;

    }
    if(!dcache_miss_inflight){
        prior_pc = pipe.PC;
    }

    if(dcache_miss_inflight  && dcache_stall_counter != 50){
      //printf("dcache_stall_counter = %d \n", dcache_stall_counter);
        if (icache_miss_inflight){
        icache_stall_counter--;
        istall = true; 
        if (icache_stall_counter == 1) {
            cache_update(Icache, icache_pending_addr);
            if(refsim_prints_comp_on){printf("icache fill at cycle %d\n", cycle_count);}

            //printf("icache fill at cycle %d\n", cycle_count);
            icache_miss_inflight = false;
            icache_cancel = false;
            icache_pending_addr = 0;
            cacheStall = false;
            //doStall = false; 
            return;
        }
        cacheStall = true;
        if(refsim_prints_comp_on){printf("icache bubble (%d)\n", icache_stall_counter);}

        //printf("icache bubble (%d)\n", icache_stall_counter);
        //printf("cache still stalling in fetch, return\n");
        return;
    }
      return; 
      return;
    }

    
    // if(stallfetch){
    //   //printf("last dcache miss cycle + 1");
    //   stallfetch = false;
    //   return;
    // }
    if (leftCycles > 0 && leftCycles < 1000) {
        leftCycles--;
        //printf("leftCycles = %d", leftCycles);
        if (leftCycles == 0) {
            RUN_BIT = 0;
            return; // done
        }
    }
// 
    // cycle_count += 1;
    if(cycle_count == 109){
       //printf("hits 109\n");
    }
    
    
    if (doStall && !icache_miss_inflight) {
      //printf("1\n");
      // STALL: do NOT fetch a new instruction
      // do NOT call bp_predict() and do NOT update pipe.PC
      //printf("stalling in fetch- return (icache)\n");
      doStall = false; 
      return;
    }
    if (flush && !icache_miss_inflight) {
       //printf("2\n");
        // do not call bp_predict
        // do not read memory
        //printf("flushing in fetch- return\n");
        flush = false; // or keep it for multiple cycles if needed
        return;
    }
    if (icache_cancel){
        //printf("Icache cancelled\n");
        icache_cancel = false;
        icache_miss_inflight = false;
        icache_pending_addr = 0;
        cacheStall = false;
        icache_stall_counter = 0;
        flush = false;
        doStall = false;
    return;
    }

    if (icache_miss_inflight){
      icache_stall_counter--;
       //printf("3\n");
       istall = true; 
        if (icache_stall_counter == 1 && (!icache_cancel)) {
            cache_update(Icache, icache_pending_addr);
            if(refsim_prints_comp_on){printf("icache fill at cycle %d\n", cycle_count);}
            icache_miss_inflight = false;
            icache_cancel = false;
            icache_pending_addr = 0;
            cacheStall = false;
            //doStall = false; 
            return;
        }
        cacheStall = true;
        if(refsim_prints_comp_on){printf("icache bubble (%d)\n", icache_stall_counter);}

        //printf("icache bubble (%d)\n", icache_stall_counter);
        //printf("cache still stalling in fetch, return\n");
        return;
    }
    else{
       //printf("4\nr");
       istall = false; 
        int result = cache_check_hit_only(Icache, pipe.PC);
        if (!result) {
           //printf("5\n");
            icache_miss_inflight = true;
            icache_pending_addr  = pipe.PC; // mask for 32-byte block
            icache_stall_counter      = 50;                // how many stall cycles remain
            // stall the pipeline
            cacheStall = true;
           // istall = true;
            //doStall = true; 
            //regIFID.instr = 0;
            //printf("cache miss (0x%lx) at cycle %d\n", pipe.PC, cycle_count);
            if(refsim_prints_comp_on){printf("cache miss (0x%lx) at cycle %d\n", pipe.PC, cycle_count);}

            return;
        }
        //printf("6\n");
        // result == HIT
        // proceed with reading the instruction from the cache
        //printf("icache hit (0x%lx) at cycle %d\n", pipe.PC, cycle_count);
        if(refsim_prints_comp_on){printf("icache hit (0x%lx) at cycle %d\n", pipe.PC, cycle_count);}

        istall = false; 
    }
    if(leftCycles >2 && (!icache_miss_inflight)){
        //printf("left cycles <= 2, %d\n", leftCycles);
        //printf("else happens\n");
        //printf("7\n");
        uint32_t instr = mem_read_32(pipe.PC);
        bp_predict(pipe.bp, pipe.PC);
        uint64_t predicted_pc = bp_predict_pc;
        if(!dcache_miss_inflight){
          regIFID.instr = instr;
          regIFID.pcIn = pipe.PC;
          regIFID.predictedPC = predicted_pc;
        }
        
        //printf("PC from 0x%lX to 0x%lX in fetch \n" , pipe.PC, predicted_pc);
        //printf("Instruction: %s\n", get_instruction_name(regIFID.instr, 0));
        pipe.PC = predicted_pc;
        stat_inst_fetch++;
    }
       //printf("8\n");
        flush = false;
        doStall = false;
}
//========================================================//
//  predictor.c                                           //
//  Source file for the Branch Predictor                  //
//                                                        //
//  Implement the various branch predictors below as      //
//  described in the README                               //
//========================================================//
#include <stdio.h>
#include <math.h>
#include "predictor.h"

//
// TODO:Student Information
//
const char *studentName = "Daniel Sanei";
const char *studentID = "A17572200";
const char *email = "dsanei@ucsd.edu";

//------------------------------------//
//      Predictor Configuration       //
//------------------------------------//

// Handy Global for use in output routines
const char *bpName[4] = {"Static", "Gshare",
                         "Tournament", "Custom"};

// define number of bits required for indexing the BHT here.
int ghistoryBits = 17;    // Number of bits used for Global History
int bpType;               // Branch Prediction Type
int verbose;

int lhistoryBits = 10;    // Number of bits used for Local History (10 bits per branch)
int pcIndexBits = 10;     // Number of bits for PC index (1024 total branches --> 2^10)

int longTageBits = 24;    // long GHR for TAGE
int mediumTageBits = 16;  // medium GHR for TAGE
int shortTageBits = 8;   // short GHR for TAGE

//------------------------------------//
//      Predictor Data Structures     //
//------------------------------------//

// gshare
uint8_t *bht_gshare;
uint64_t ghistory;

// tournament predictor: global, local, chooser

// global predictor
uint8_t *bht_tglobal;   // global BHT (2-bit saturating counter for predictions)
uint64_t tghistory;     // GHR: global history register (tracks last N global branch outcomes)
  // 8 bits (1 byte) are smallest addressable unit --> minimum bits

// local predictor
uint8_t *bht_tlocal;    // local BHT (2-bit saturating counter for local predictions)
uint16_t *lht;          // LHT: local history table (tracks history per branch)
  // Alpha 21264 processor holds 10 bits of branch history for up to 1024 prediction counters
  // thus, 16 bits covers 10 bits needed per entry, and 1024 entries in the local BHT
    // LHT uses PC to index into BHT, which provides prediction for that local branch 

// chooser table
uint8_t *chooser;       // chooser table to decide between global vs. local (2-bit saturating counter)

// custom predictor: adjusted tournament predictor, except GHR split into 3 different sizes

// global predictor (short, medium, long GHR)
uint8_t *bht_tage_long;     // 32-bit GHR
uint64_t ghistory_long;
uint8_t *bht_tage_medium;   // 16-bit GHR
uint64_t ghistory_medium;
uint8_t *bht_tage_short;    // 8-bit GHR
uint64_t ghistory_short;

// local predictor
uint8_t *bht_local_tage;
uint16_t *lht_tage;

// chooser table
uint8_t *chooser_tage;


//------------------------------------//
//        Predictor Functions         //
//------------------------------------//

// Initialize the predictor
//

// custom functions
void init_tage()
{
  // get total entries
  int global_bht_entries_long = 1 << longTageBits;      // 17 bits
  int global_bht_entries_medium = 1 << mediumTageBits;  // 16 bits
  int global_bht_entries_short = 1 << shortTageBits;    // 14 bits

  // initialize each bht with correct size
  bht_tage_long = (uint8_t *)malloc(global_bht_entries_long * sizeof(uint8_t));
  bht_tage_medium = (uint8_t *)malloc(global_bht_entries_medium * sizeof(uint8_t));
  bht_tage_short = (uint8_t *)malloc(global_bht_entries_short * sizeof(uint8_t));

  // initialize all predictions to weakly taken
  for ( int i = 0; i < global_bht_entries_long; i++ ) {     // 32 bits
    bht_tage_long[i] = WN;
  }
  ghistory_long = 0;
  for ( int i = 0; i < global_bht_entries_medium; i++ ) {   // 16 bits
    bht_tage_medium[i] = WN;
  }
  ghistory_medium = 0;
  for ( int i = 0; i < global_bht_entries_short; i++ ) {    // 8 bits
    bht_tage_short[i] = WN;
  }
  ghistory_short = 0;

  // local predictor (BHT local)
  int local_bht_entries = 1 << lhistoryBits;      // total entries (predictions per branch)
  bht_local_tage = (uint8_t *)malloc(local_bht_entries * sizeof(uint8_t));  // BHT size
  for ( int i = 0; i < local_bht_entries; i++ ) {
    bht_local_tage[i] = WN;  // each entry --> weakly not taken
  } // 1024 entries for 2-bit counters (where counter for each branch)

  // local predictor (LHT)
  int local_history_entries = 1 << pcIndexBits;   // total entries (history per branch)
  lht_tage = (uint16_t *)malloc(local_history_entries * sizeof(uint16_t));   // LHT size
  for ( int i = 0; i < local_history_entries; i++ ) {
    lht_tage[i] = 0;  // each branch --> empty local history
  } // 1024 entries for each branch

  // chooser table
  int chooser_entries = 1 << shortTageBits;  // index chooser table with short global history
  chooser_tage = (uint8_t *)malloc(chooser_entries * sizeof(uint8_t));   // chooser table size
  for ( int i = 0; i < chooser_entries; i++ ) {
    chooser_tage[i] = WN;  // each entry --> weakly not taken
  }
}

uint8_t tage_predict(uint32_t pc)
{
  // long global predictor
  uint32_t long_bht_entries = 1 << longTageBits;                      // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_long = pc & (long_bht_entries - 1);          // extract lower bits of PC
  uint32_t long_lower_bits = ghistory_long & (long_bht_entries - 1);  // extract lower bits of GHR
  uint32_t long_index = pc_lower_bits_long ^ long_lower_bits;         // XOR the PC, GHR to get global BHT index

  // medium global predictor
  uint32_t medium_bht_entries = 1 << mediumTageBits;                  // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_medium = pc & (medium_bht_entries - 1);      // extract lower bits of PC
  uint32_t medium_lower_bits = ghistory_medium & (medium_bht_entries - 1);   // extract lower bits of GHR
  uint32_t medium_index = pc_lower_bits_medium ^ medium_lower_bits;   // XOR the PC, GHR to get global BHT index

  // short global predictor
  uint32_t short_bht_entries = 1 << shortTageBits;                    // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_short = pc & (short_bht_entries - 1);        // extract lower bits of PC
  uint32_t short_lower_bits = ghistory_short & (short_bht_entries - 1);     // extract lower bits of GHR
  uint32_t short_index = pc_lower_bits_short ^ short_lower_bits;      // XOR the PC, GHR to get global BHT index

  // local predictor
  uint32_t local_lht_entries = 1 << pcIndexBits;                                // 2^pcIndexBits = LHT entries
  uint32_t pc_lower_bits_local = pc & (local_lht_entries - 1);                  // extract lower bits of PC
  uint32_t local_bht_entries = 1 << lhistoryBits;                               // 2^lhistoryBits = local BHT entries
  uint32_t local_index = lht_tage[pc_lower_bits_local] & (local_bht_entries - 1);    // index into local BHT using LHT as index
                                                                                    // for branch-specific history

  // make long global prediction (index into global BHT)
  uint8_t long_prediction;
  if ( bht_tage_long[long_index] >= WT ) {      // 10 or 11
    long_prediction = TAKEN;
  } else {                                      // 01 or 00
    long_prediction = NOTTAKEN;
  }

  // make medium global prediction (index into global BHT)
  uint8_t medium_prediction;
  if ( bht_tage_medium[medium_index] >= WT ) {  // 10 or 11
    medium_prediction = TAKEN;
  } else {                                      // 01 or 00
    medium_prediction = NOTTAKEN;
  }

  // make short global prediction (index into global BHT)
  uint8_t short_prediction;
  if ( bht_tage_short[short_index] >= WT ) {    // 10 or 11
    short_prediction = TAKEN;
  } else {                                      // 01 or 00
    short_prediction = NOTTAKEN;
  }

  // make local prediction (index into local BHT using LHT as index)
  uint8_t local_prediction;
  if ( bht_local_tage[local_index] >= WT ) {    // 10 or 11
    local_prediction = TAKEN;
  } else {                                      // 01 or 00
    local_prediction = NOTTAKEN;
  }

  // chooser index set to short global index by default
  uint32_t chooser_entries = 1 << 10;
  uint32_t chooser_index = ghistory_short & (chooser_entries - 1);
  uint8_t chooser_prediction = chooser_tage[chooser_index];

  // select most accurate prediction
  if ( chooser_prediction < WN ) {          // favor local if < 1 (i.e. = 0)
    return local_prediction;
  } else if (chooser_prediction < WT ) {    // favor short global if < 2 (i.e. = 1)
    return short_prediction;
  } else if (chooser_prediction < ST ) {    // favor medium global if < 3 (i.e. = 2)
    return medium_prediction;
  } else {                                  // favor long global if else (i.e. = 3)
    return long_prediction;
  }
}

void train_tage(uint32_t pc, uint8_t outcome)
{
  // long global predictor
  uint32_t long_bht_entries = 1 << longTageBits;                      // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_long = pc & (long_bht_entries - 1);          // extract lower bits of PC
  uint32_t long_lower_bits = ghistory_long & (long_bht_entries - 1);       // extract lower bits of GHR
  uint32_t long_index = pc_lower_bits_long ^ long_lower_bits;         // XOR the PC, GHR to get global BHT index

  // medium global predictor
  uint32_t medium_bht_entries = 1 << mediumTageBits;                  // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_medium = pc & (medium_bht_entries - 1);      // extract lower bits of PC
  uint32_t medium_lower_bits = ghistory_medium & (medium_bht_entries - 1);   // extract lower bits of GHR
  uint32_t medium_index = pc_lower_bits_medium ^ medium_lower_bits;   // XOR the PC, GHR to get global BHT index

  // short global predictor
  uint32_t short_bht_entries = 1 << shortTageBits;                    // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_short = pc & (short_bht_entries - 1);        // extract lower bits of PC
  uint32_t short_lower_bits = ghistory_short & (short_bht_entries - 1);     // extract lower bits of GHR
  uint32_t short_index = pc_lower_bits_short ^ short_lower_bits;      // XOR the PC, GHR to get global BHT index

  // local predictor
  uint32_t local_lht_entries = 1 << pcIndexBits;                                // 2^pcIndexBits = LHT entries
  uint32_t pc_lower_bits_local = pc & (local_lht_entries - 1);                  // extract lower bits of PC
  uint32_t local_bht_entries = 1 << lhistoryBits;                               // 2^lhistoryBits = local BHT entries
  uint32_t local_index = lht_tage[pc_lower_bits_local] & (local_bht_entries - 1);    // index into local BHT using LHT as index
                                                                                    // for branch-specific history

  // make long global prediction (index into global BHT)
  uint8_t long_prediction;
  if ( bht_tage_long[long_index] >= WT ) {      // 10 or 11
    long_prediction = TAKEN;
  } else {                                      // 01 or 00
    long_prediction = NOTTAKEN;
  }

  // make medium global prediction (index into global BHT)
  uint8_t medium_prediction;
  if ( bht_tage_medium[medium_index] >= WT ) {  // 10 or 11
    medium_prediction = TAKEN;
  } else {                                      // 01 or 00
    medium_prediction = NOTTAKEN;
  }

  // make short global prediction (index into global BHT)
  uint8_t short_prediction;
  if ( bht_tage_short[short_index] >= WT ) {    // 10 or 11
    short_prediction = TAKEN;
  } else {                                      // 01 or 00
    short_prediction = NOTTAKEN;
  }

  // make local prediction (index into local BHT using LHT as index)
  uint8_t local_prediction;
  if ( bht_local_tage[local_index] >= WT ) {    // 10 or 11
    local_prediction = TAKEN;
  } else {                                      // 01 or 00
    local_prediction = NOTTAKEN;
  }

  // chooser index set to short global index by default
  uint32_t chooser_entries = 1 << shortTageBits;
  uint32_t chooser_index = ghistory_short & (chooser_entries - 1);
  uint8_t chooser_prediction = chooser_tage[chooser_index];

  // update predictors based on outcome
  if ( outcome == TAKEN ) {   // branch taken, checks to prevent 2-bit counter from going over upper bound
    if ( bht_tage_long[long_index] < ST ) {
      bht_tage_long[long_index]++;
    }
    if ( bht_tage_medium[medium_index] < ST ) {
      bht_tage_medium[medium_index]++;
    }
    if ( bht_tage_short[short_index] < ST ) {
      bht_tage_short[short_index]++;
    }
    if ( bht_local_tage[local_index] < ST ) {
      bht_local_tage[local_index]++;
    }
  } else {   // branch not taken, checks to prevent 2-bit counter from going under lower bound
    if ( bht_tage_long[long_index] > SN ) {
      bht_tage_long[long_index]--;
    }
    if ( bht_tage_medium[medium_index] > SN ) {
      bht_tage_medium[medium_index]--;
    }
    if ( bht_tage_short[short_index] > SN ) {
      bht_tage_short[short_index]--;
    }
    if ( bht_local_tage[local_index] > SN ) {
      bht_local_tage[local_index]--;
    }
  }

  // update chooser table by comparing global, local predictors
  uint8_t final_prediction;
  if ( chooser_prediction < WN ) {            // favor local if < 1 (i.e. = 0)
    final_prediction = local_prediction;
  }
  else if ( chooser_prediction < WT ) {       // favor short global if < 2 (i.e. = 1)
    final_prediction = short_prediction;
  }
  else if ( chooser_prediction < ST ) {       // favor medium global if < 3 (i.e. = 2)
    final_prediction = medium_prediction;
  }
  else {                                      // favor long global if else (i.e. = 3)
    final_prediction = long_prediction;
  }

  // compare final  prediction to outcome
  if ( final_prediction == outcome ) {
    if ( chooser_tage[chooser_index] < ST ) {      // prevent 2-bit counter from going over upper bound
      chooser_tage[chooser_index]++;
    }
    
  }
  else {
    if ( chooser_tage[chooser_index] > SN ) {      // prevent 2-bit counter from going under lower bound
      chooser_tage[chooser_index]--;
    }
  }

  // update LHT (left bitwise shift, then add new outcome)
  uint16_t old_lht = lht_tage[pc_lower_bits_local];
  uint16_t new_lht = (old_lht << 1) | outcome;
  lht_tage[pc_lower_bits_local] = new_lht & ((1 << lhistoryBits) - 1);

  // update long, mediun, short GHRs (left bitwise shift, then add new outcome)
  uint64_t old_ghr_long = ghistory_long;
  uint64_t new_ghr_long = (old_ghr_long << 1) | outcome;
  ghistory_long = new_ghr_long & ((1 << longTageBits) - 1);
  uint64_t old_ghr_medium = ghistory_medium;
  uint64_t new_ghr_medium = (old_ghr_medium << 1) | outcome;
  ghistory_medium = new_ghr_medium & ((1 << mediumTageBits) - 1);
  uint64_t old_ghr_short = ghistory_short;
  uint64_t new_ghr_short = (old_ghr_short << 1) | outcome;
  ghistory_short = new_ghr_short & ((1 << shortTageBits) - 1);
}

void cleanup_tage()
{
  free(bht_tage_long);
  free(bht_tage_medium);
  free(bht_tage_short);
  free(bht_local_tage);
  free(lht_tage);
  free(chooser_tage);
}

// tournament functions
void init_tournament()
{
  // global predictor
  int global_bht_entries = 1 << ghistoryBits;   // total entries
  bht_tglobal = (uint8_t *)malloc(global_bht_entries * sizeof(uint8_t));  // initialize BHT with correct size
  for ( int i = 0; i < global_bht_entries; i++ ) {
    bht_tglobal[i] = WN;  // each entry --> weakly not taken
  }
  tghistory = 0;  // empty global history

  // local predictor (BHT local)
  int local_bht_entries = 1 << lhistoryBits;      // total entries (predictions per branch)
  bht_tlocal = (uint8_t *)malloc(local_bht_entries * sizeof(uint8_t));  // BHT size
  for ( int i = 0; i < local_bht_entries; i++ ) {
    bht_tlocal[i] = WN;  // each entry --> weakly not taken
  } // 1024 entries for 2-bit counters (where counter for each branch)

  // local predictor (LHT)
  int local_history_entries = 1 << pcIndexBits;   // total entries (history per branch)
  lht = (uint16_t *)malloc(local_history_entries * sizeof(uint16_t));   // LHT size
  for ( int i = 0; i < local_history_entries; i++ ) {
    lht[i] = 0;  // each branch --> empty local history
  } // 1024 entries for each branch

  // chooser table
  int chooser_entries = 1 << ghistoryBits;  // total entries
  chooser = (uint8_t *)malloc(chooser_entries * sizeof(uint8_t));   // chooser table size
  for ( int i = 0; i < chooser_entries; i++ ) {
    chooser[i] = WN;  // each entry --> weakly not taken
  }
}

uint8_t tournament_predict(uint32_t pc)
{
  // gshare predictor (similar to standalone gshare)
  uint32_t global_bht_entries = 1 << ghistoryBits;                      // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_global = pc & (global_bht_entries - 1);        // extract lower bits of PC
  uint32_t ghistory_lower_bits = ghistory & (global_bht_entries - 1);   // extract lower bits of GHR
  uint32_t global_index = pc_lower_bits_global ^ ghistory_lower_bits;   // XOR the PC, GHR to get global BHT index

  // local predictor
  uint32_t local_lht_entries = 1 << pcIndexBits;                                // 2^pcIndexBits = LHT entries
  uint32_t pc_lower_bits_local = pc & (local_lht_entries - 1);                  // extract lower bits of PC
  uint32_t local_bht_entries = 1 << lhistoryBits;                               // 2^lhistoryBits = local BHT entries
  uint32_t local_index = lht[pc_lower_bits_local] & (local_bht_entries - 1);    // index into local BHT using LHT as index
                                                                                    // for branch-specific history

  // make global prediction (index into global BHT)
  uint8_t global_prediction;
  if ( bht_tglobal[global_index] >= WT ) {  // 10 or 11
    global_prediction = TAKEN;
  } else {                                  // 01 or 00
    global_prediction = NOTTAKEN;
  }

  // make local prediction (index into local BHT using LHT as index)
  uint8_t local_prediction;
  if ( bht_tlocal[local_index] >= WT ) {    // 10 or 11
    local_prediction = TAKEN;
  } else {                                  // 01 or 00
    local_prediction = NOTTAKEN;
  }

  // chooser index set to global index by default
  uint32_t chooser_index = global_index;

  // select most accurate prediction
  uint8_t chooser_prediction = chooser[chooser_index];
  if ( chooser_prediction >= WT ) {         // favor local
    return local_prediction;
  } else {
    return global_prediction;               // favor global
  }
}

void train_tournament(uint32_t pc, uint8_t outcome)
{
  // gshare predictor (similar to standalone gshare)
  uint32_t global_bht_entries = 1 << ghistoryBits;                      // 2^ghistoryBits = global BHT entries
  uint32_t pc_lower_bits_global = pc & (global_bht_entries - 1);        // extract lower bits of PC
  uint32_t ghistory_lower_bits = ghistory & (global_bht_entries - 1);   // extract lower bits of GHR
  uint32_t global_index = pc_lower_bits_global ^ ghistory_lower_bits;   // XOR the PC, GHR to get global BHT index

  // local predictor
  uint32_t local_lht_entries = 1 << pcIndexBits;                                // 2^pcIndexBits = LHT entries
  uint32_t pc_lower_bits_local = pc & (local_lht_entries - 1);                  // extract lower bits of PC
  uint32_t local_bht_entries = 1 << lhistoryBits;                               // 2^lhistoryBits = local BHT entries
  uint32_t local_index = lht[pc_lower_bits_local] & (local_bht_entries - 1);    // index into local BHT using LHT as index
                                                                                    // for branch-specific history

  // make global prediction (index into global BHT)
  uint8_t global_prediction;
  if ( bht_tglobal[global_index] >= WT ) {  // 10 or 11
    global_prediction = TAKEN;
  } else {                                  // 01 or 00
    global_prediction = NOTTAKEN;
  }

  // make local prediction (index into local BHT using LHT as index)
  uint8_t local_prediction;
  if ( bht_tlocal[local_index] >= WT ) {    // 10 or 11
    local_prediction = TAKEN;
  } else {                                  // 01 or 00
    local_prediction = NOTTAKEN;
  }

  // chooser index set to global index by default
  uint32_t chooser_index = global_index;

  // update global, local predictors based on actual outcome
  if ( outcome == TAKEN ) {   // branch taken

    // global predictor
    if ( bht_tglobal[global_index] < ST ) {     // prevent 2-bit counter from going over upper bound
      bht_tglobal[global_index]++;
    }

    // local predictor
    if ( bht_tlocal[local_index] < ST ) {       // prevent 2-bit counter from going over upper bound
      bht_tlocal[local_index]++;
    }

  }
  else {    // branch not taken

    // global predictor
    if ( bht_tglobal[global_index] > SN ) {   // prevent 2-bit counter from going under lower bound
      bht_tglobal[global_index]--;
    }

    // local predictor
    if ( bht_tlocal[local_index] > SN ) {     // prevent 2-bit counter from going under lower bound
      bht_tlocal[local_index]--;
    }
  }

  // update chooser table by comparing global, local predictors
  if ( global_prediction != local_prediction ) {

    // local is correct (increment 2-bit counter of chooser)
    if ( local_prediction == outcome && chooser[chooser_index] < ST ) {   // prevent 2-bit counter from going over upper bound
      chooser[chooser_index]++;
    }

    // global is correct (decrement 2-bit counter of chooser)
    else if ( global_prediction == outcome && chooser[chooser_index] > SN ) {  // prevent 2-bit counter from going under lower bound
      chooser[chooser_index]--;
    }
  }

  // update LHT (left bitwise shift, then add new outcome)
  uint16_t old_lht = lht[pc_lower_bits_local];
  uint16_t new_lht = (old_lht << 1) | outcome;
  lht[pc_lower_bits_local] = new_lht & ((1 << lhistoryBits) - 1);

  // update GHR (left bitwise shift, then add new outcome)
  uint64_t old_ghr = ghistory;
  uint64_t new_ghr = (old_ghr << 1) | outcome;
  ghistory = new_ghr & ((1 << ghistoryBits) - 1);
}

void cleanup_tournament()
{
  free(bht_tlocal);
  free(bht_tglobal);
  free(lht);
  free(chooser);
}

// gshare functions
void init_gshare()
{
  // get total number of BHT entries, where bitwise shift effectively finds 2^(ghistoryBits)
  int bht_entries = 1 << ghistoryBits;
  // each entry in bht only needs 2 bits, but smallest addressable memory is 8 bits = 1 byte
  bht_gshare = (uint8_t *)malloc(bht_entries * sizeof(uint8_t));  // allocate dynamic memory for 2-bit saturating counter
  int i = 0;
  for (i = 0; i < bht_entries; i++)
  {
    bht_gshare[i] = WN;   // initialize each BHT entry to weakly not taken
  }
  ghistory = 0;   // initialize empty global history
}

uint8_t gshare_predict(uint32_t pc)
{
  // get lower ghistoryBits of pc
  uint32_t bht_entries = 1 << ghistoryBits;
  // extract lower bits of program counter
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  // extract lower bits of global history register
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  // XOR lower bits of PC and GHR to get index of branch prediction
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;
  // get bht entry of index to retrieve branch prediction
  switch (bht_gshare[index])
  {
  case WN:
    return NOTTAKEN;
  case SN:
    return NOTTAKEN;
  case WT:
    return TAKEN;
  case ST:
    return TAKEN;
  default:
    printf("Warning: Undefined state of entry in GSHARE BHT!\n");
    return NOTTAKEN;
  }
}

void train_gshare(uint32_t pc, uint8_t outcome)
{
  // get lower ghistoryBits of pc
  uint32_t bht_entries = 1 << ghistoryBits;
  // extract lower bits of program counter
  uint32_t pc_lower_bits = pc & (bht_entries - 1);
  // extract lower bits of global history register
  uint32_t ghistory_lower_bits = ghistory & (bht_entries - 1);
  // XOR lower bits of PC and GHR to get index of branch prediction
  uint32_t index = pc_lower_bits ^ ghistory_lower_bits;

  // Update state of entry in bht based on outcome
  switch (bht_gshare[index])
  {
  case WN:
    bht_gshare[index] = (outcome == TAKEN) ? WT : SN;
    break;
  case SN:
    bht_gshare[index] = (outcome == TAKEN) ? WN : SN;
    break;
  case WT:
    bht_gshare[index] = (outcome == TAKEN) ? ST : WN;
    break;
  case ST:
    bht_gshare[index] = (outcome == TAKEN) ? ST : WT;
    break;
  default:
    printf("Warning: Undefined state of entry in GSHARE BHT!\n");
    break;
  }

  // Update history register
  ghistory = ((ghistory << 1) | outcome);
    // update with actual outcome for latest branch
}

void cleanup_gshare()
{
  free(bht_gshare);
}

void init_predictor()
{
  switch (bpType)
  {
  case STATIC:
    break;
  case GSHARE:
    init_gshare();
    break;
  case TOURNAMENT:
    init_tournament();
    break;
  case CUSTOM:
    init_tage();
    break;
  default:
    break;
  }
}

// Make a prediction for conditional branch instruction at PC 'pc'
// Returning TAKEN indicates a prediction of taken; returning NOTTAKEN
// indicates a prediction of not taken
//
uint32_t make_prediction(uint32_t pc, uint32_t target, uint32_t direct)
{

  // Make a prediction based on the bpType
  switch (bpType)
  {
  case STATIC:
    return TAKEN;
  case GSHARE:
    return gshare_predict(pc);
  case TOURNAMENT:
    return tournament_predict(pc);
  case CUSTOM:
    return tage_predict(pc);
  default:
    break;
  }

  // If there is not a compatable bpType then return NOTTAKEN
  return NOTTAKEN;
}

// Train the predictor the last executed branch at PC 'pc' and with
// outcome 'outcome' (true indicates that the branch was taken, false
// indicates that the branch was not taken)
//

void train_predictor(uint32_t pc, uint32_t target, uint32_t outcome, uint32_t condition, uint32_t call, uint32_t ret, uint32_t direct)
{
  if (condition)
  {
    switch (bpType)
    {
    case STATIC:
      return;
    case GSHARE:
      return train_gshare(pc, outcome);
    case TOURNAMENT:
      return train_tournament(pc, outcome);
    case CUSTOM:
      return train_tage(pc, outcome);
    default:
      break;
    }
  }
}

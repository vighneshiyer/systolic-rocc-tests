// See LICENSE for license details.

#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef BAREMETAL
#include <sys/mman.h>
#endif
#include "include/gemmini_testutils.h"

#define NO_BIAS 0
#define REPEATING_BIAS 1

#define A_TRANSPOSE 0
#define B_TRANSPOSE 0

#ifndef BAREMETAL
#define MAT_DIM_I 512
#define MAT_DIM_K 512
#define MAT_DIM_J 512
#else

// #define MAT_DIM_I 128
// #define MAT_DIM_K 128
// #define MAT_DIM_J 128

#define MAT_DIM_I 32
#define MAT_DIM_K 1000
#define MAT_DIM_J 32

// #define MAT_DIM_I 256
// #define MAT_DIM_K 512
// #define MAT_DIM_J 512

#endif

#define L2SIZE 524288
#define L2LINESIZE 64
#define NUMDIRTY 10
#define SEED 111

#if A_TRANSPOSE==0
#define A_STRIDE MAT_DIM_K
#else
#define A_STRIDE MAT_DIM_I
#endif

#if B_TRANSPOSE==0
#define B_STRIDE MAT_DIM_J
#else
#define B_STRIDE MAT_DIM_K
#endif

unsigned int random_seed = SEED;
/* taken from the glibc implementation
 * https://sourceware.org/git/?p=glibc.git;a=blob;f=stdlib/rand_r.c;h=30fafc0fa5f30592470a1796bc129f2b1613ecec;hb=HEAD
*/  
int seeded_rand (unsigned int *seed)
{
  unsigned int next = *seed;
  int result;

  next *= 1103515245;
  next += 12345;
  result = (unsigned int) (next / 65536) % 2048;

  next *= 1103515245;
  next += 12345;
  result <<= 10;
  result ^= (unsigned int) (next / 65536) % 1024;

  next *= 1103515245;
  next += 12345;
  result <<= 10;
  result ^= (unsigned int) (next / 65536) % 1024;

  *seed = next;

  return result;
}


int main() {
#ifndef BAREMETAL
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
      perror("mlockall failed");
      exit(1);
    }
#endif


    static char llc_read_array[L2SIZE] row_align(1);
    int acc = 0;

    //flush the cache
    for (int j=0; j < 3; j++)
    { 
      for (int i=0; i < ((L2SIZE + L2LINESIZE - 1) / L2LINESIZE); i++) {
	    acc += llc_read_array[((i+j)*L2LINESIZE) % L2SIZE];
      }
    }

    //generate dirty cache lines
    for (int i=0; i < NUMDIRTY; i++) {
	    //llc_read_array[(i*(L2SIZE/NUMDIRTY)) % L2SIZE] = acc;
            int addr = seeded_rand(&random_seed) % L2SIZE;
	    llc_read_array[addr] = acc;
    }

    gemmini_flush(0);

#if A_TRANSPOSE==0
    static elem_t full_A[MAT_DIM_I][MAT_DIM_K] row_align(1);
#else
    static elem_t full_A[MAT_DIM_K][MAT_DIM_I] row_align(1);
#endif

#if B_TRANSPOSE==0
    static elem_t full_B[MAT_DIM_K][MAT_DIM_J] row_align(1);
#else
    static elem_t full_B[MAT_DIM_J][MAT_DIM_K] row_align(1);
#endif

    static elem_t full_C[MAT_DIM_I][MAT_DIM_J] row_align(1);
    static acc_t full_D[MAT_DIM_I][MAT_DIM_J] row_align_acc(1);

    static full_t gold_full[MAT_DIM_I][MAT_DIM_J];
    static elem_t gold[MAT_DIM_I][MAT_DIM_J];

    printf("Starting gemmini matmul\n");
    printf("I: %d, J: %d, K: %d\n", MAT_DIM_I, MAT_DIM_J, MAT_DIM_K);
    printf("NO_BIAS: %d, REPEATING_BIAS: %d\n", NO_BIAS, REPEATING_BIAS);
    printf("A_TRANSPOSE: %d, B_TRANSPOSE: %d\n", A_TRANSPOSE, B_TRANSPOSE);
    unsigned long start = read_cycles();

    tiled_matmul_auto(MAT_DIM_I, MAT_DIM_J, MAT_DIM_K,
            (elem_t*)full_A, (elem_t*)full_B, NO_BIAS ? NULL : &full_D[0][0], (elem_t*)full_C,
            A_STRIDE, B_STRIDE, MAT_DIM_J, MAT_DIM_J,
            MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY, MVIN_SCALE_IDENTITY,
            NO_ACTIVATION, ACC_SCALE_IDENTITY, 0, REPEATING_BIAS,
            A_TRANSPOSE, B_TRANSPOSE,
            false, false,
            1, true,
            WS);

    unsigned long end = read_cycles();
    printf("Cycles taken: %u\n", end-start);

    const int total_macs = MAT_DIM_I * MAT_DIM_J * MAT_DIM_K;
    const int ideal_cycles = total_macs / (DIM * DIM);
    const int utilization = 100 * ideal_cycles / (end-start);
    printf("Utilization: %d%%\n", utilization);

  exit(0);
}


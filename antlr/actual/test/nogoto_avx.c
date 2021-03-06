#include<stdio.h>
#include<stdlib.h>
#include<pthread.h>
#include<papi.h>
#include<time.h>
#include <x86intrin.h>

#include "fpp.h"
#include "cuckoo.h"

int *keys;
struct cuckoo_bkt *ht_index;

int sum = 0;
int succ_1 = 0;		/** < Number of lookups that succeed in bucket 1 */
int succ_2 = 0;		/** < Number of lookups that success in bucket 2 */
int fail = 0;		/** < Failed lookups */

const union {
  uint64_t val[4];
  __m256d _mask;
} _mask = {.val = {0x00000000ffffFFFFull, 0x00000000ffffFFFFull,
				   0x00000000ffffFFFFull, 0x00000000ffffFFFFull}};

int find_index_avx(uint64_t addr, uint64_t *table) {
	// assert((uintptr_t)(table) % 32 == 0);

	__m256d _addr = (__m256d)_mm256_set1_epi64x(addr & 0x00000000ffffFFFFull);
	__m256d _table = _mm256_load_pd((double *)table);
	_table = _mm256_and_pd(_table, _mask._mask);
	__m256d cmp = _mm256_cmp_pd(_addr, _table, _CMP_EQ_OQ);

	return __builtin_ffs(_mm256_movemask_pd(cmp));
}

struct cuckoo_slot slot;
uint64_t* slot_ptr = (uint64_t*)(&slot);

// batch_index must be declared outside process_batch
int batch_index = 0;

void process_batch(int *key_lo)
{
	foreach(batch_index, BATCH_SIZE) {
		int bkt_1, bkt_2, slot_id, success = 0;
		slot.key = key_lo[batch_index];

		bkt_1 = hash(slot.key) & NUM_BKT_;
		slot_id = find_index_avx(*slot_ptr, (uint64_t*)(&ht_index[bkt_1]));
		if(slot_id) {
			sum += ht_index[bkt_1].slot[slot_id-1].value;
			succ_1 ++;
			success = 1;
		}

		if(success == 0) {
			bkt_2 = hash(bkt_1) & NUM_BKT_;
			slot_id = find_index_avx(*slot_ptr, (uint64_t*)(&ht_index[bkt_2]));
			if(slot_id) {
				sum += ht_index[bkt_2].slot[slot_id-1].value;
				succ_2 ++;
				success = 1;
			}
		}

		if(success == 0) {
			fail ++;
		}
	}
}

int main(int argc, char **argv)
{
	int i;

	/** < Variables for PAPI */
	float real_time, proc_time, ipc;
	long long ins;
	int retval;

#ifndef __AVX__
	// Check whether we have AVX
	assert(1==0);
#endif

	// A short piece of code for testing how we
	// should set up the mask.
	struct cuckoo_slot slot;
	slot.key = 2312;
	slot.value = 2342;
	uint64_t* slot_ptr = (uint64_t*)(&slot);
	struct cuckoo_slot slot_1;
	slot_1.key = 2312;
	slot_1.value = 231232;
	uint64_t* slot_1_ptr = (uint64_t*)(&slot_1);
	assert(((*slot_1_ptr)&0x00000000ffffFFFFull) == ((*slot_ptr)&0x00000000ffffFFFFull));

	red_printf("main: Initializing cuckoo hash table\n");
	cuckoo_init(&keys, &ht_index);

	red_printf("main: Starting lookups\n");
	/** < Init PAPI_TOT_INS and PAPI_TOT_CYC counters */
	if((retval = PAPI_ipc(&real_time, &proc_time, &ins, &ipc)) < PAPI_OK) {
		printf("PAPI error: retval: %d\n", retval);
		exit(1);
	}

	for(i = 0; i < NUM_KEYS; i += BATCH_SIZE) {
		process_batch(&keys[i]);
	}

	if((retval = PAPI_ipc(&real_time, &proc_time, &ins, &ipc)) < PAPI_OK) {
		printf("PAPI error: retval: %d\n", retval);
		exit(1);
	}

	red_printf("Time = %.4f s, rate = %.2f\n"
		"Instructions = %lld, IPC = %f\n"
		"sum = %d, succ_1 = %d, succ_2 = %d, fail = %d\n",
		real_time, NUM_KEYS / real_time,
		ins, ipc,
		sum, succ_1, succ_2, fail);

	return 0;
}

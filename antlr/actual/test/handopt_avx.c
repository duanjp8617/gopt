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

struct cuckoo_slot slots[BATCH_SIZE];
uint64_t* slot_ptrs[BATCH_SIZE];

// batch_index must be declared outside process_batch
int batch_index = 0;

void process_batch(int *key_lo)
{
	int slot_id, bkt_1[BATCH_SIZE], bkt_2[BATCH_SIZE];
	int success[BATCH_SIZE] = {0};

	/** < Issue prefetch for the 1st bucket*/
	for(batch_index = 0; batch_index < BATCH_SIZE; batch_index ++) {
		slots[batch_index].key = key_lo[batch_index];

		bkt_1[batch_index] = hash(key_lo[batch_index]) & NUM_BKT_;
		__builtin_prefetch(&ht_index[bkt_1[batch_index]], 0, 0);
	}


	/** < Try the 1st bucket. If it fails, issue prefetch for bkt #2 */
	for(batch_index = 0; batch_index < BATCH_SIZE; batch_index ++) {
		slot_id = find_index_avx(*slot_ptrs[batch_index], (uint64_t*)(&ht_index[bkt_1[batch_index]]));
		if(slot_id) {
			sum += ht_index[bkt_1[batch_index]].slot[slot_id-1].value;
			succ_1 ++;
			success[batch_index] = 1;
		}

		if(success[batch_index] == 0) {
			bkt_2[batch_index] = hash(bkt_1[batch_index]) & NUM_BKT_;
			__builtin_prefetch(&ht_index[bkt_2[batch_index]], 0, 0);
		}
	}

	/** < For failed batch elements, try the 2nd bucket */
	for(batch_index = 0; batch_index < BATCH_SIZE; batch_index ++) {

		if(success[batch_index] == 0) {
			slot_id = find_index_avx(*slot_ptrs[batch_index], (uint64_t*)(&ht_index[bkt_2[batch_index]]));
			if(slot_id) {
				sum += ht_index[bkt_2[batch_index]].slot[slot_id-1].value;
				succ_2 ++;
				success[batch_index] = 1;
			}

			if(success[batch_index] == 0) {
				fail ++;
			}
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

	// Set up slot_ptrs
	int i_;
	for(i_=0; i_<BATCH_SIZE; i_++) {
		slot_ptrs[i_] = (uint64_t*)(&slots[i_]);
	}

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

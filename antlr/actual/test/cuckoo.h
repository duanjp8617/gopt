#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <assert.h>
#include "city.h"

// Number of slot per bucket
#define SLOTS_PER_BUCKET 4

/** < Number of cuckoo buckets */
/** < 256 KB: L2 cache */
//#define NUM_BKT (4 * 1024)
//#define NUM_BKT_ (NUM_BKT - 1)

/** < 16 MB: L3 cache */
//#define NUM_BKT (256 * 1024)
//#define NUM_BKT_ (NUM_BKT - 1)

/** < 512 MB: RAM */
#define NUM_BKT (16 * 1024 * 1024)
#define NUM_BKT_ (NUM_BKT - 1)

/** < Number of keys inserted into the hash table */
#define NUM_KEYS ((SLOTS_PER_BUCKET/2) * NUM_BKT)
#define NUM_KEYS_ (NUM_KEYS - 1)

/** < Key for shmget */
#define CUCKOO_KEY 1

struct cuckoo_slot
{
	int key;
	int value;
};

struct cuckoo_bkt
{
	struct cuckoo_slot slot[SLOTS_PER_BUCKET];
};

static inline int hash(int u)
{
	return CityHash64((char *) &u, 4);
}

void cuckoo_init(int **keys, struct cuckoo_bkt** ht_index);
void red_printf(const char *format, ...);


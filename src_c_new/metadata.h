#ifndef __NVM_METADATA_H__
#define __NVM_METADATA_H__

#include "globals.h"
#include <libpmemobj.h>


typedef struct metadata_st {
    int num_pools;
    uint64_t inst_num;
} metadata;

POBJ_LAYOUT_BEGIN(init);
POBJ_LAYOUT_TOID(init, metadata);
POBJ_LAYOUT_END(init);

typedef struct metadata_root_str {
    TOID(metadata) init_metadata;
} metadata_root;

TOID_DECLARE_ROOT(metadata_root);

void initialize_metadata();

void update_num_pools(int numpools);

int retrieve_num_pools();


#endif // !__NVM_METADATA_H__
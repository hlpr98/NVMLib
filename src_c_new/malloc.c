#include "malloc.h"
#include "types.h"
#include "algo.h"
#include "math.h"
#include <stdint.h>
#include <libiberty/splay-tree.h>
// #include <splay-tree.h>
#include "object_maintainance.h"
#include <uv.h>

// MEMoid __memalloc(size_t size) {
//     MEMoid new_obj;

//     if (decide_allocation(size) - NVRAM_HEAP == 0) {
//         // allocate in NVRAM
//         new_obj.pool_id = get_current_poolid();
//         new_obj.offset = get_first_free_offset(size);

//     } else if (decide_allocation(size) - DRAM_HEAP == 0) {
//         // allocate in DRAM
//         new_obj.offset = (uint64_t)(malloc(size));
//         new_obj.pool_id = POOL_ID_MALLOC_OBJ;
//     }
//     new_obj.size = size;

//     // new_obj.access_bitmap = (uint64_t*)malloc((ceil((double)size/64));
//     return new_obj;
// }
splay_tree addr2MemOID_read;
splay_tree addr2MemOID_write;

uint64_t free_slot_size(TOID(struct pool_free_slot) slot) {
    return D_RO(slot)->end_b - D_RO(slot)->start_b + 1;
}

/**
 * Allocates first free memory chunk of the given `size` in the given `pool`
 * specified by `pool_id`.
 * 
 * @param pool_id: the ID of the `pool` where the allocation needs to be made.
 * @param size: the `size` of memory that needs to be allocated.
 * @return The `offset` of the *memory chuck* allocated. This `offset` is from the 
 * first address of the `pool`.
 * 
 * @note This is an *internal function*. It is not exposed to other files.
 * @see allot_first_free_offset()
 */
uint64_t allot_first_free_offset_pool(uint64_t pool_id, size_t size) {
    // LOG shit
    pool_free_slot_val temp;
    pool_free_slot_val* temp_ptr = &temp;
    temp.key = pool_id;
#ifdef DEBUG
    printf("allot_free_slot_offset_pool temp_ptr->pool_id %d\n", temp_ptr->key);
#endif
    int hmret = HASH_MAP_FIND(pool_free_slot_val)(pool_free_slot_map, &temp_ptr);
#ifdef DEBUG
    printf("affop find = %d\n", hmret);
#endif
    pool_free_slot_head *f_head = temp_ptr->head;
#ifdef DEBUG
    printf("fhead value is %p\n", f_head);
#endif
    uint64_t ret = -1;
    int flag = 0;
    TOID(struct pool_free_slot) node;
    POBJ_TAILQ_FOREACH (node, f_head, fnd) {
        if (free_slot_size(node) == size) {
            ret = D_RO(node)->start_b;
            flag = 1;
            break;
        } else if (free_slot_size(node) > size) {
            ret = D_RO(node)->start_b;
            uint64_t new_start = D_RO(node)->start_b + size;
        #ifdef DEBUG
            printf("Allot flag 2\n");
        #endif
            TX_BEGIN(temp_ptr->pool) {
                D_RW(node)->start_b = new_start;
            } TX_END
            break;
        }
    }

    if (flag == 1) {
        TX_BEGIN(temp_ptr->pool) {
            POBJ_TAILQ_REMOVE_FREE(f_head, node, fnd);
        } TX_END
    }
    return ret;
}


/**
 * Allocates the first free memory chunk of the given `size`.
 * 
 * @param size: the `size` of memory that needs to be allocated.
 * @return `MEMoid` object.
 * 
 * @attention This function is responsible to create a new `pool` iff 
 * **any of the previous pools** are insufficient for the asked `size`.
 * @see allot_first_free_offset_pool()
 */
MEMoid allot_first_free_offset(size_t size) {
#ifdef DEBUG
    printf("allot_frst_free_offset num_pools = %d\n", num_pools);
#endif
    for (int i = 1; i <= num_pools; i++) {
        uint64_t ret = allot_first_free_offset_pool(i, size);
        if (ret >= 0) {
            MEMoid m;
            m.pool_id = i;
            m.offset = ret;
            m.size = size;
            return m;
        }
    }
    create_new_pool(size);
    uint64_t ret = allot_first_free_offset_pool(num_pools, size);
    MEMoid m;
    m.pool_id = num_pools;
    m.offset = ret;
    m.size = size;
    return m;
}


/**
 * Allocates the memory requested and returns a MEMoid.
 * 
 * @param size: the `size` of the object that needs to be allocated.
 * @param which_ram: the RAM in which the allocation must happen.
 * @return The `MEMoid` object after allocation is completed.
 * 
 * @note This is an *internal function*. It is not exposed to other files.
 * @see _memalloc() memalloc()
 */
MEMoid __memalloc(size_t size, int which_ram) {
    MEMoid new_obj;

    switch(which_ram) {
        case ANY_RAM:
            if (decide_allocation(size) - NVRAM_HEAP == 0) {
                // allocate in NVRAM
                //new_obj.pool_id = get_current_poolid();
                //new_obj.offset = get_first_free_offset(size);
                new_obj = allot_first_free_offset(size);

            } else if (decide_allocation(size) - DRAM_HEAP == 0) {
                // allocate in DRAM
                new_obj.offset = (uint64_t)(malloc(size));
                new_obj.pool_id = POOL_ID_MALLOC_OBJ;
            }
            break;

        case NVRAM_HEAP:
            // allocate in NVRAM
            //new_obj.pool_id = get_current_poolid();
            //new_obj.offset = get_first_free_offset(size);
            new_obj = allot_first_free_offset(size);
            break;

        case DRAM_HEAP:
            // allocate in DRAM
            new_obj.offset = (uint64_t)(malloc(size));
            new_obj.pool_id = POOL_ID_MALLOC_OBJ;
            break;

        default:
            break;
    }
    new_obj.size = size;

    // new_obj.access_bitmap = (uint64_t*)malloc((ceil((double)size/64));
    return new_obj;
}

uint64_t string_hash(const char *str) {
    uint64_t hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

/**
 * Allocates the memory requested and returns a MEMoidKey.
 * 
 * @param size: the `size` of the object that needs to be allocated.
 * @param file: the `file` where the *requested* object is defined.
 * @param func: the `function` where the *requested* object is defined.
 * @param line: the `line` number where the *requested* object is defined.
 * @param which_ram (optional): the RAM in which the allocation must happen.
 * @return The `MEMoidkey` object after allocation is completed.
 * 
 * @see __memalloc() memalloc()
 * @attention This function is responsible for checking if the *requested* object is present in NVRAM
 * from the previous run (to ensure `crash consistency`).
 * @attention It has to add the created objects into `object_maintainance table`, `type_table` and `splay_tree`.
 */
MEMoidKey _memalloc(size_t size, const char *file, const char *func, const int line, int num_args, ...) {
    // Can be made faster ... but thats an after thought as of now xD
    MEMoidKey key = (((string_hash(file) + string_hash(func)) % __UINT64_MAX__)
                    + line) % __UINT64_MAX__;

    int which_ram = ANY_RAM;
    va_list arg_list;
    va_start(arg_list, num_args);
    while(num_args--) {
        which_ram = va_arg(arg_list, int);
    }

    // Check if the element is already in NVRAM
    MEMoid oid = get_MEMoid(key);
#ifdef DEBUG
    printf("\n------------------------------------\nmemalloc: oid->pool_id = %ld,  oid->offset = %ld\n------------------------------------------------\n", oid.pool_id, oid.offset);
#endif

    if ((oid.offset == MEMOID_NULL.offset && oid.pool_id == MEMOID_NULL.pool_id) || oid.pool_id == POOL_ID_MALLOC_OBJ){
        // Need to create the new object

    #ifdef DEBUG
        printf("memalloc: Creating new object or it was previously in DRAM\n");
    #endif

        oid =  __memalloc(size, which_ram);
        
        // Get the hashmap mutex first to ensure there are no leftover accesses
        uv_mutex_lock(&object_maintainence_hashmap_mutex);
        // need to replace the value
        remove_object_from_hashmap(key); // This is done because there is chance that the object remains after free
        insert_object_to_hashmap(key, oid);
        uv_mutex_unlock(&object_maintainence_hashmap_mutex);

        // Insert into the object maintainance table for logistics
        uv_mutex_lock(&object_maintainence_maintain_map_mutex);
        insert_into_maintainance_map(create_new_maintainance_map_entry(key, oid, oid.pool_id==POOL_ID_MALLOC_OBJ?DRAM:NVRAM, which_ram==ANY_RAM ? true:false));
        uv_mutex_unlock(&object_maintainence_maintain_map_mutex);
    } else {
        // obj is already present in the map

    #ifdef DEBUG
        printf("memalloc: Object already existing and is from NVRAM\n");
    #endif

        //DRAM variables in the table need to be replaced
        // Get the hashmap mutex first to ensure there are no leftover accesses
        // uv_mutex_lock(&object_maintainence_hashmap_mutex);
        // if(oid.pool_id == POOL_ID_MALLOC_OBJ) {
        //     // dram object
        //     oid =  __memalloc(size, DRAM);
        //     // need to replace the value
        //     remove_object_from_hashmap(key);
        //     // Insert in the main types table
        //     insert_object_to_hashmap(key, oid);
        // }
        // uv_mutex_unlock(&object_maintainence_hashmap_mutex);

        // Insert into the object maintainance table for logistics
        uv_mutex_lock(&object_maintainence_addtion_mutex);
        object_maintainance_addition* to_add = (object_maintainance_addition*)malloc(sizeof(object_maintainance_addition));

        to_add->key = key;
        to_add->oid = oid;
        to_add->can_be_moved = which_ram==ANY_RAM ? true:false;
        to_add->which_ram = oid.pool_id==POOL_ID_MALLOC_OBJ?DRAM:NVRAM;
        TAILQ_INSERT_TAIL(&addition_queue_head, to_add, list);

        // insert_into_maintainance_map(create_new_maintainance_map_entry(key, oid, oid.pool_id==POOL_ID_MALLOC_OBJ?DRAM:NVRAM, true));
        uv_mutex_unlock(&object_maintainence_addtion_mutex);
    }

    struct addr2memoid_key* new_key = (struct addr2memoid_key*)malloc(sizeof(addr2memoid_key));
    new_key->comp = cmp_node;
    new_key->key = key;

    struct addr2memoid_key* new_key1 = (struct addr2memoid_key*)malloc(sizeof(addr2memoid_key));
    new_key1->comp = cmp_node;
    new_key1->key = key;

    uv_mutex_lock(&write_splay_tree_mutex);
    splay_tree_insert(addr2MemOID_write, (uintptr_t)new_key, NULL);
    uv_mutex_unlock(&write_splay_tree_mutex);

    uv_mutex_lock(&read_splay_tree_mutex);
    splay_tree_insert(addr2MemOID_read, (uintptr_t)new_key1, NULL);
    uv_mutex_unlock(&read_splay_tree_mutex);

    return key;
}

// MEMoidKey _memalloc(size_t size, uint8_t which_ram, const char *file, const char *func, const int line) {
//     // Can be made faster ... but thats an after thought as of now xD
//     MEMoidKey key = (((string_hash(file) + string_hash(func)) % __UINT64_MAX__)
//                     + line) % __UINT64_MAX__;
//     MEMoid oid =  __memalloc(size);
//     // Insert in the main types table
//     insert_object_to_hashmap(key, oid);
//     // Insert into the object maintainance table for logistics
//     insert_into_maintainance_map(create_new_maintainance_map_entry(key, oid, oid.pool_id==POOL_ID_MALLOC_OBJ?DRAM:NVRAM, false));
//     struct addr2memoid_key* new_key = (struct addr2memoid_key*)malloc(sizeof(addr2memoid_key));
//     new_key->comp = cmp_node;
//     new_key->key = key;
//     splay_tree_insert(addr2MemOID, new_key, NULL);
//     return key;
// }


/**
 * The function to return the actual `memptr` for a given `MEMoid` object.
 * 
 * @param oid: the `MEMoid` object.
 * @return The memory address represented by `oid`.
 */
void* get_memobj_direct(MEMoid oid) {
    if (oid.offset == 0 && oid.pool_id == POOL_ID_MALLOC_OBJ){
        return NULL;
    }

    switch(oid.pool_id){
        case POOL_ID_MALLOC_OBJ:
            // It is a malloc object
            return (void *)((uintptr_t)oid.offset);

        default:
            // It is a nvm object
            return (void *)((uintptr_t)get_pool_from_poolid(oid.pool_id) + oid.offset);
    }
}

// static inline void _memfree(MEMoidKey oidkey, size_t size) {
//     MEMoid oid = get_MEMoid(oidkey);
//     if(oid.offset == MEMOID_NULL.offset && oid.pool_id == MEMOID_NULL.pool_id) {
//         switch(oid.pool_id) {
//             case POOL_ID_MALLOC_OBJ:
//                 free(oid.offset);
//                 break;

//             default:
//                 nvm_free(oid.pool_id, oid.offset, size);
//         }
//     }

//     // remove the entry from the HashTable
//     remove_object_from_hashmap(oidkey);
// }

/**
 * The fucntion to free the allocated memory location.
 * 
 * @param oidkey: the `MEMoidKey` of the object whose memory needs to be freed.
 * @return Nothing.
 * 
 * @note This function doesn't call nvm_free() or `free()` by itself, instead it enques 
 * the *deletion task* for the `deletion thread` to take care of it.
 */
void _memfree(MEMoidKey oidkey) {
    // Just hand over the task to `Deletion thread`

    object_maintainance *found_obj = find_in_maintainance_map(oidkey);
    if (!found_obj)
        return;

    uv_mutex_lock(&object_maintainence_addtion_mutex);
    object_maintainance_deletion* to_del = (object_maintainance_deletion*)malloc(sizeof(object_maintainance_deletion));

    to_del->key = found_obj->key;
    to_del->oid = found_obj->oid;
    to_del->can_be_moved = found_obj->can_be_moved;
    to_del->which_ram = NO_RAM;
    TAILQ_INSERT_TAIL(&deletion_queue_head, to_del, list);
    // insert_into_maintainance_map(create_new_maintainance_map_entry(key, oid, oid.pool_id==POOL_ID_MALLOC_OBJ?DRAM:NVRAM, true));
    uv_mutex_unlock(&object_maintainence_addtion_mutex);

    // found_obj->which_ram = NO_RAM; // Delete the object
}



#define MEMOID_FIRST(m) (get_pool_from_poolid(m.pool_id) + m.offset) //need to use this in object_maintenence.c also, move to malloc.h?
#define MEMOID_LAST(m) (get_pool_from_poolid(m.pool_id) + m.offset + m.size)

void* _key_get_first(MEMoidKey key) {
    MEMoid m = get_MEMoid(key);
    if (m.offset == MEMOID_NULL.offset && m.pool_id == MEMOID_NULL.pool_id) {
        return NULL;
    }
    return (void *)MEMOID_FIRST(m);
}

void* _key_get_last(MEMoidKey key) {
    MEMoid m = get_MEMoid(key);
    if (m.offset == MEMOID_NULL.offset && m.pool_id == MEMOID_NULL.pool_id) {
        return NULL;
    }
    return (void *)MEMOID_FIRST(m) + m.size;
}

/**
 * The compare function used by the `splay_tree`.
 * 
 * @param key1: the `splay_tree_key` of one of the elements that needs to be compared.
 * @param key2: the `splay_tree_key` of the other element that needs to be compared.
 * 
 * @return 0: if both are equivalent.
 * @return the direction in which the search should continue, if the two elements are not equivalent.
 */
int addr2memoid_cmp(splay_tree_key key1, splay_tree_key key2) {
    if (((addr2memoid_key*)key2)->comp == cmp_node && ((addr2memoid_key*)key1)->comp == cmp_node) {
        //printf("splay tree comp node key1addr = %p key2addr = %p\n", KEY_FIRST(((addr2memoid_key*)key1)->key), KEY_FIRST(((addr2memoid_key*)key2)->key));
        if (((addr2memoid_key*)key1)->key == ((addr2memoid_key*)key2)->key)
            return 0;
        else
            return KEY_FIRST(((addr2memoid_key*)key1)->key) > KEY_FIRST(((addr2memoid_key*)key2)->key)?1:-1;

    } else if (((addr2memoid_key*)key2)->comp == cmp_addr) {
        //printf("splay tree comp addr key1addr = %p key2addr = %p\n", KEY_FIRST(((addr2memoid_key*)key1)->key), ((addr2memoid_key*)key2)->addr);
        if (((addr2memoid_key*)key2)->addr >= KEY_FIRST(((addr2memoid_key*)key1)->key) &&
            ((addr2memoid_key*)key2)->addr < KEY_LAST(((addr2memoid_key*)key1)->key))
            return 0;
        else
            return KEY_FIRST(((addr2memoid_key*)key1)->key) > ((addr2memoid_key*)key2)->addr?1:-1;
    } else if (((addr2memoid_key*)key1)->comp == cmp_addr) {
        //printf("splay tree comp addr key1addr = %p key2addr = %p\n", ((addr2memoid_key*)key1)->addr, KEY_FIRST(((addr2memoid_key*)key2)->key));
        if (((addr2memoid_key*)key1)->addr >= KEY_FIRST(((addr2memoid_key*)key2)->key) &&
            ((addr2memoid_key*)key1)->addr < KEY_LAST(((addr2memoid_key*)key2)->key))
            return 0;
        else
            return KEY_FIRST(((addr2memoid_key*)key2)->key) > ((addr2memoid_key*)key1)->addr?-1:1;
    }
    printf("Something wrong.\n");
    return 0;
}

void addr2memoid_del(splay_tree_key key) {
    free((void*)key);
}

// The splay tree is used for reverse mapping
// from address to MemOiD
void init_splay() {
    addr2MemOID_read = splay_tree_new(addr2memoid_cmp, addr2memoid_del, NULL);
    addr2MemOID_write = splay_tree_new(addr2memoid_cmp, addr2memoid_del, NULL);
}

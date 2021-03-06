#include "globals.h"
#include <libpmemobj.h>
#include <string.h>
#include "metadata.h"
#include "pool.h"
#include "malloc.h"
#include "types.h"
#include "object_maintainance.h"
#include "mem_log.h"

extern char *program_invocation_short_name;

/// Open the logging files
void open_logging_files() {
    main_log_file_fd = fopen(MAIN_LOG_FILE_NAME, "w");
}

/**
 * Initializes all the metadata and data pools and loads corresponding
 * information into `hashmaps`.
 */
void initialize() {
    init_splay();

    /// Initialize `types` hashtable
    init_types_table();

    /// Initialize metadata and open metadata pools
    initialize_metadata();

    /// Open data pools
    initialize_pool();

    /// Opening the logging files
    open_logging_files();
    initialise_logistics();
    initialize_log_queues();
}

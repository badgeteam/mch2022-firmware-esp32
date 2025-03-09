#pragma once

#ifdef BOOTLOADER_BUILD

/*
The appfs wrapper layer wraps (using thr gcc linker wrapping function, see the CMakeFiles.txt
file in this directory) the calls that the rest of the bootloader uses, and maps accesses to
the appfs partition to transparently map to access to the selected appfs file instead.
*/

// Initialize the wrapper. Handle is a handle to the appfs file, part_start and part_size
// must refer to the offset and the size of the appfs partition.
void appfs_wrapper_init(appfs_handle_t handle, size_t part_start, size_t part_size);

// Un-initialize the wrapper. Flash access will always access raw flash after this.
void appfs_wrapper_deinit();

#endif

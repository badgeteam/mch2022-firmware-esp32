#include <stdint.h>
#include <stdbool.h>

bool download_file(const char* url, const char* path);
bool download_ram(const char* url, uint8_t** ptr, size_t* size);

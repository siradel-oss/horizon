#include <cstddef>

extern void* stbi_malloc(size_t size);
extern void* stbi_realloc(void* ptr, size_t new_size);
extern void stbi_free(void* ptr);

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

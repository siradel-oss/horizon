#include <assert.h>
#include <lz4hc.h>

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    if (argc < 3) return EXIT_FAILURE;

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];

    FILE* in_fp = fopen(input_filename, "rb");
    if (!in_fp) return EXIT_FAILURE;

    fseek(in_fp, 0, SEEK_END);
    size_t input_size = ftell(in_fp);
    fseek(in_fp, 0, SEEK_SET);
    char* input = (char*)malloc(input_size);
    size_t read_count = fread(input, input_size, 1, in_fp);
    if (read_count != 1) return EXIT_FAILURE;
    fclose(in_fp);

    size_t compression_buffer_size = LZ4_compressBound(input_size);
    char* compression_buffer = (char*)malloc(compression_buffer_size);
    size_t output_size = LZ4_compress_HC(
        input, compression_buffer, input_size, compression_buffer_size, LZ4HC_CLEVEL_MAX);

    FILE* out_fp = fopen(output_filename, "wb+");
    if (!out_fp) return EXIT_FAILURE;

    fwrite(compression_buffer, output_size, 1, out_fp);
    fclose(out_fp);

    free(input);
    free(compression_buffer);

    return EXIT_SUCCESS;
}

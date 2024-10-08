#include <arpa/inet.h>
#include "crc.h"
#include <stdlib.h>  // For malloc() and exit()
#include <errno.h>   // For perror() and errno
#include "zutil.h"   // For mem_def() and mem_inf()
#include "lab_png.h" // For simple PNG data structures
#include <sys/types.h>
#include <sys/stat.h> // For lstat function
#include <unistd.h>   // For POSIX API including lstat
#include <dirent.h>
#include <stdio.h> // For printf() and perror()
#include <string.h>

int is_png(U8 *buf, size_t n)
{
    // The PNG signature is exactly 8 bytes long, so if n is less than 8, it's not a PNG.
    if (n < 8)
    {
        return 0; // Not enough data to verify if it's a PNG.
    }

    // Define the PNG signature as a reference.
    U8 png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    // Compare the first 8 bytes of buf to the PNG signature.
    if (memcmp(buf, png_signature, 8) == 0)
    {
        return 1; // It is a PNG file.
    }
    else
    {
        return 0; // It is not a PNG file.
    }
}

void get_png_data_IHDR(const char *filename, struct data_IHDR *out, FILE *fp, long offset, int whence)
{
    // Seek to the specified offset in the file
    if (fseek(fp, offset, whence) != 0)
    {
        printf("Error: Failed to seek to IHDR chunk position.\n");
        return;
    }

    // Read the IHDR chunk length and type
    unsigned char chunk_len[4];
    fread(chunk_len, 1, 4, fp); // Chunk length (should be 13 for IHDR)

    int len = (chunk_len[3] << 24) | (chunk_len[2] << 16) | ((chunk_len[1] << 8)) | (chunk_len[0]);
    len = ntohl(len);

    // Read the IHDR data (13 bytes)
    U8 ihdr_data[len + 4];
    fread(ihdr_data, 1, len + 4, fp);
    // Extract width and height

    out->width = ntohl(*(uint32_t *)&ihdr_data[4]);
    out->height = ntohl(*(uint32_t *)&ihdr_data[8]);

    U32 crc_val;
    crc_val = crc(ihdr_data, len + 4);
    U8 crc_img[4];
    fread(crc_img, 1, 4, fp);
    U32 crc_img_len = ((U8)crc_img[0] << 24) | ((U8)crc_img[1] << 16) | (((U8)crc_img[2] << 8)) | ((U8)crc_img[3]);
    if (crc_img_len != crc_val)
    {
        printf(" IHDR chunk CRC error: computed %x expected %x\n", crc_val, crc_img_len);
    }
}
int get_png_height(struct data_IHDR *buf)
{
    return buf->height;
}
int get_png_width(struct data_IHDR *buf)
{
    return buf->width;
}
simple_PNG_p mallocPNG()
{
    // need to allocate memory for the simple_PNG structure
    simple_PNG_p png = malloc(sizeof(struct simple_PNG));

    // Check if memory allocation was successful
    if (png == NULL)
    {
        printf("Memory allocation for simple_PNG failed.\n");
        return NULL;
    }

    return png;
}
void free_png(simple_PNG_p in)
{
    if (in != NULL)
    {

        free(in);
    }
}

int write_PNG(char *filepath, simple_PNG_p png)
{
    FILE *fp = fopen(filepath, "wb");
    if (!fp)
    {
        printf("Error: Could not open file for writing.\n");
        return -1;
    }

    //  PNG signature
    U8 png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (fwrite(png_signature, 1, 8, fp) != 8)
    {
        printf("Error: Could not write PNG signature.\n");
        fclose(fp);
        return -1;
    }

    //  IHDR chunk
    if (write_chunk(fp, png->p_IHDR) != 0)
    {
        printf("Error: Could not write IHDR chunk.\n");
        fclose(fp);
        return -1;
    }

    //  IDAT chunk
    if (write_chunk(fp, png->p_IDAT) != 0)
    {
        printf("Error: Could not write IDAT chunk.\n");
        fclose(fp);
        return -1;
    }

    //  IEND chunk
    if (write_chunk(fp, png->p_IEND) != 0)
    {
        printf("Error: Could not write IEND chunk.\n");
        fclose(fp);
        return -1;
    }

    // Close the file
    fclose(fp);

    return 0; // Success
}
int write_chunk(FILE *fp, struct chunk *chunk)
{

    U32 length_be = chunk->length;
    U32 crc_be = chunk->crc;

    //  length
    fwrite(&length_be, 1, 4, fp);

    //  type
    fwrite(chunk->type, 1, 4, fp);

    //  data
    fwrite(chunk->p_data, 1, ntohl(chunk->length), fp);

    //  CRC
    fwrite(&crc_be, 4, 1, fp);

    return 0;
}

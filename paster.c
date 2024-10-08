#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <curl/curl.h>
#include <stdbool.h>
#include "crc.h"
#include <errno.h>    // For perror() and errno
#include "zutil.h"    // For mem_def() and mem_inf()
#include "lab_png.h"  // For simple PNG data structures
#include <sys/stat.h> // For lstat function
#include <dirent.h>
#include <arpa/inet.h>
#include <pthread.h> // Include pthread.h for pthread_mutex_t and other pthread functions

#define DUM_URL "https://example.com/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC 524288    /* 1024*512  = 0.5M */
#define TOTAL_SEGMENTS 50 // Total segments required to reconstruct the image
typedef unsigned char U8;

typedef struct recv_buf
{
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct thread_data
{
    int id;
    const char *server;
    int image_seg;
    int server_num;
} thread_data;

pthread_mutex_t mutex;
RECV_BUF recv_bufs[TOTAL_SEGMENTS];     // Global buffer to hold all segments
bool received_segments[TOTAL_SEGMENTS]; // Track received segments
int completed_segments = 0;             // Counter for completed segments

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

    if (realsize > strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0)
    {
        /* Extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

/**
 * @brief Write callback function to save a copy of received data in RAM.
 */
size_t write_cb_curl(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size)
    {
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + (BUF_INC > realsize + 1 ? BUF_INC : realsize + 1);
        char *q = realloc(p->buf, new_size);
        if (q == NULL)
        {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /* copy data from libcurl */
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = malloc(max_size);
    if (p == NULL)
    {
        return 1; // Memory allocation failed
    }

    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; // Valid seq should be non-negative
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL)
    {
        return 1; // Invalid pointer
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

/**
 * @brief Output data in memory to a file
 */
int write_file(const char *path, const void *in, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL)
    {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len)
    {
        fprintf(stderr, "write_file: incomplete write!\n");
        fclose(fp);
        return -3;
    }
    return fclose(fp);
}

void *get_image(void *arg)
{
    thread_data *td = (thread_data *)arg;
    CURL *curl_handle;
    CURLcode res;
    char url[256];
    RECV_BUF precv_buf; // Local buffer for each thread

    // Initialize the local buffer
    recv_buf_init(&precv_buf, BUF_SIZE);

    curl_handle = curl_easy_init();
    if (curl_handle == NULL)
    {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        pthread_exit(NULL);
    }

    while (completed_segments < TOTAL_SEGMENTS)
    {
        sprintf(url, "%s:2520/image?img=%d", td->server, td->image_seg);
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&precv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&precv_buf);
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            break;
        }

        if (precv_buf.seq >= 0 && precv_buf.seq < TOTAL_SEGMENTS)
        {
            pthread_mutex_lock(&mutex);

            if (!received_segments[precv_buf.seq])
            {

                received_segments[precv_buf.seq] = true;
                completed_segments++;

                recv_bufs[precv_buf.seq].buf = malloc(precv_buf.size);
                if (recv_bufs[precv_buf.seq].buf == NULL)
                {
                    perror("malloc");
                    pthread_mutex_unlock(&mutex);
                    break;
                }
                memcpy(recv_bufs[precv_buf.seq].buf, precv_buf.buf, precv_buf.size);
                recv_bufs[precv_buf.seq].size = precv_buf.size;
                recv_bufs[precv_buf.seq].max_size = precv_buf.size;
                recv_bufs[precv_buf.seq].seq = precv_buf.seq;

                printf("Received segment %d from server %d (%s)\n", precv_buf.seq, td->server_num, td->server);
            }
            pthread_mutex_unlock(&mutex);
        }

        precv_buf.size = 0;
        precv_buf.seq = -1;
    }

    // Cleanup
    curl_easy_cleanup(curl_handle);
    recv_buf_cleanup(&precv_buf);
    free(td);
    pthread_exit(NULL);
}

void catpng(int argc, char *argv[])
{
    // 1) create a data buffer to store the image data

    U8 *data_buffer = NULL;
    U64 initial_data_buffer_len = 0;
    int ret = 0;
    U32 final_png_height = 0;
    U32 final_png_width = 0;
    U64 final_def_buff_len = 0;

    data_buffer = (U8 *)malloc(initial_data_buffer_len);

    for (int i = 1; i < argc; i++)
    {
        const char *filename = argv[i];
        FILE *file = fopen(filename, "rb");
        if (!file)
        {
            printf("Error: Could not reopen file %s\n", filename);
            return;
        }

        // next we need to read the header of the file and make sure it is a png
        U8 buffer[8];
        size_t bytes_read = fread(buffer, 1, 8, file);
        if (bytes_read < 8)
        {
            printf("Error: Could not read file or file too short.\n");
            fclose(file);
            return;
        }
        if (!is_png(buffer, bytes_read))
        {
            printf("%s is NOT a PNG file.\n", filename);
            fclose(file);
            return;
        }
        else
        {
            // need to get the IHDR data mainly height and width of the png
            struct data_IHDR ihdr;
            get_png_data_IHDR(filename, &ihdr, file, 8, SEEK_SET);
            int height_png = get_png_height(&ihdr);
            int width_png = get_png_width(&ihdr);

            final_png_height += height_png;
            final_png_width = width_png;

            // next we need to extract the IDAT data
            // Read the IDAT chunk length and type
            U32 chunk_len_IDAT;
            fread(&chunk_len_IDAT, 1, 4, file);

            // reading the chuck type
            U8 chunk_type_IDAT[4];
            fread(chunk_type_IDAT, 1, 4, file);

            U32 IDAT_chunck_len = ntohl(chunk_len_IDAT);

            // Read the IDAT data
            U8 *IDAT_data = malloc(sizeof(U8) * IDAT_chunck_len);
            fread(IDAT_data, 1, IDAT_chunck_len, file);

            U8 IDAT_crc_img[4];
            fread(IDAT_crc_img, 1, 4, file);

            // array to store the inflated data
            U8 *gp_buf_inf = malloc(height_png * (width_png * 4 + 1));
            U64 gp_buf_inf_len = height_png * (width_png * 4 + 1);

            ret = mem_inf(gp_buf_inf, &gp_buf_inf_len, IDAT_data, IDAT_chunck_len);
            if (ret != 0)
            { /* faliur */
                fprintf(stderr, "mem_def failed. ret = %d.\n", ret);
            }
            final_def_buff_len += gp_buf_inf_len;
            data_buffer = realloc(data_buffer, final_def_buff_len);

            memcpy(data_buffer + (final_def_buff_len - gp_buf_inf_len), gp_buf_inf, gp_buf_inf_len);
            free(IDAT_data);
            free(gp_buf_inf);
        }
        fclose(file);
    }

    // array to store the compressed data
    U64 gp_buf_def_len = 0;

    U8 *gp_buf_def = malloc((final_def_buff_len));

    ret = mem_def(gp_buf_def, &gp_buf_def_len, data_buffer, final_def_buff_len, Z_DEFAULT_COMPRESSION);
    if (ret != 0)
    { /* failure */
        fprintf(stderr, "mem_def failed. ret = %d.\n", ret);
        return;
    }

    simple_PNG_p all_png = mallocPNG();
    // need to set the IHDR, IDAT AND IEND for this newly created all_png
    struct chunk IHDR_all;
    IHDR_all.length = htonl(13);
    IHDR_all.type[0] = 'I';
    IHDR_all.type[1] = 'H';
    IHDR_all.type[2] = 'D';
    IHDR_all.type[3] = 'R';

    struct data_IHDR ihdr_data_all;
    ihdr_data_all.width = htonl(final_png_width);
    ihdr_data_all.height = htonl(final_png_height);
    ihdr_data_all.bit_depth = 8;
    ihdr_data_all.color_type = 6;
    ihdr_data_all.compression = 0;
    ihdr_data_all.filter = 0;
    ihdr_data_all.interlace = 0;

    IHDR_all.p_data = (U8 *)&ihdr_data_all;

    // calculating the crc
    U8 crc_data[13 + 4];
    memcpy(crc_data, IHDR_all.type, 4);
    memcpy(crc_data + 4, IHDR_all.p_data, 13);

    U32 IHDR_crc_val;
    IHDR_crc_val = crc(crc_data, 13 + 4);
    IHDR_all.crc = htonl(IHDR_crc_val);

    // creating the IDAT data chuck
    struct chunk IDAT_all;
    IDAT_all.length = htonl((U32)gp_buf_def_len);
    IDAT_all.p_data = gp_buf_def;
    IDAT_all.type[0] = 'I';
    IDAT_all.type[1] = 'D';
    IDAT_all.type[2] = 'A';
    IDAT_all.type[3] = 'T';

    // calculating the crc
    U8 crc_data_IDAT[gp_buf_def_len + 4];
    memcpy(crc_data_IDAT, IDAT_all.type, 4);
    memcpy(crc_data_IDAT + 4, IDAT_all.p_data, gp_buf_def_len);

    U32 IDAT_crc_val;
    IDAT_crc_val = crc(crc_data_IDAT, gp_buf_def_len + 4);
    IDAT_all.crc = htonl(IDAT_crc_val);

    // creating the IEND data chuck
    struct chunk IEND_all;
    IEND_all.length = 0;
    IEND_all.type[0] = 'I';
    IEND_all.type[1] = 'E';
    IEND_all.type[2] = 'N';
    IEND_all.type[3] = 'D';

    IEND_all.p_data = NULL;

    // calculating the crc
    U8 crc_data_IEND[IEND_all.length + 4];
    memcpy(crc_data_IEND, IEND_all.type, 4);
    memcpy(crc_data_IEND + 4, IEND_all.p_data, IEND_all.length);

    U32 IEND_crc_val;
    IEND_crc_val = crc(crc_data_IEND, IEND_all.length + 4);
    IEND_all.crc = htonl(IEND_crc_val);

    all_png->p_IHDR = &IHDR_all;
    all_png->p_IDAT = &IDAT_all;
    all_png->p_IEND = &IEND_all;

    write_PNG("all.png", all_png);

    free_png(all_png);
    free(gp_buf_def);
    free(data_buffer);
}

int main(int argc, char *argv[])
{
    int num_threads = 1;
    int image_num = 1;

    const char *servers[] = {
        "http://ece252-1.uwaterloo.ca",
        "http://ece252-2.uwaterloo.ca",
        "http://ece252-3.uwaterloo.ca"};

    if (argc > 1)
    {
        if (strcmp(argv[1], "-t") == 0 && argc > 2)
        {
            num_threads = atoi(argv[2]);
            if (num_threads < 1 || num_threads > 20)
            {
                fprintf(stderr, "Number of threads must be between 1 and 20.\n");
                return 1;
            }
        }
        if (strcmp(argv[3], "-n") == 0 && argc > 2)
        {
            image_num = atoi(argv[4]);
            if (image_num < 1 || image_num > 3)
            {
                fprintf(stderr, "Image number must be 1, 2, or 3.\n");
                return 1;
            }
        }
    }

    pthread_t threads[num_threads];
    pthread_mutex_init(&mutex, NULL);
    printf("Number of threads: %d\n", num_threads);
    printf("Image number: %d\n", image_num);
    pid_t pid = getpid();
    // Create threads
    for (int i = 0; i < num_threads; i++)
    {
        thread_data *td = malloc(sizeof(thread_data));
        td->id = i;
        td->server = servers[i % 3]; // Assign servers in round-robin
        td->image_seg = image_num;
        td->server_num = (i % 3) + 1;
        pthread_create(&threads[i], NULL, get_image, (void *)td);
    }

    // Join threads after they are all created
    for (int i = 0; i < num_threads; i++)
    {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&mutex);

    char strings[50][256];
    char *string_ptrs[50]; // Array of pointers to strings
    for (size_t i = 0; i < 50; i++)
    {
        sprintf(strings[i], "./output_%zu_%d.png", i, pid);
        string_ptrs[i] = strings[i]; // Initialize the array of pointers
    }

    for (size_t i = 0; i < 50; i++)
    {
        char fname[256];
        sprintf(fname, "./output_%d_%d.png", recv_bufs[i].seq, pid);
        write_file(fname, recv_bufs[i].buf, recv_bufs[i].size);
    }
    printf("All segments received: %s\n", strings[0]);
    catpng(49, string_ptrs);

    for (size_t i = 0; i < 50; i++)
    {
        char fname[256];
        sprintf(fname, "./output_%d_%d.png", recv_bufs[i].seq, pid);

        // Delete the file
        if (remove(fname) != 0)
        {
            perror("Error deleting file");
        }
    }

    // Cleanup buffers after image is saved
    for (int i = 0; i < TOTAL_SEGMENTS; i++)
    {
        if (recv_bufs[i].buf != NULL)
        {
            free(recv_bufs[i].buf);
        }
    }

    printf("All segments received: %d\n", completed_segments);
    return 0;
}

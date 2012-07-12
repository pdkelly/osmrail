/*
 * osmrail - OpenStreetMap filter for railway-related features
 * Copyright (C) 2011 Paul D Kelly
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <pthread.h>
#include <bzlib.h>

/* Maximum block size used in bzip2 compression. We always try to read uncompressed
 * blocks of this size, to be efficient. */
#define BLOCK_SIZE 900000

struct osm_planet
{
    FILE *fp;              /**< File pointer to open compressed file */
    BZFILE *bzfp;          /**< Abstract file pointer used by bzip2 library */
    unsigned char buff[2][BLOCK_SIZE]; /**< Double buffer to hold data read in file read thread */
    int buff_len[2];       /**< Number of bytes placed in buffer by file read thread */

    char buff_filled[2];   /**< Boolean; indictes that buffer contains data */
    /* Mutexes and signals to signal between reader and writer threads that
     * buffers have been drained or filled */
    pthread_mutex_t drained_mutex, filled_mutex;
    pthread_cond_t drained_signal, filled_signal;

    pthread_t file_read_thread;
    char exit_now;         /**< Boolean; tells file read thread to exit prematurely */
    char finished;         /**< Boolean; set to true when file read thread has terminated */

    unsigned char curr_read_buff; /**< Current buffer being read from */
    int curr_read_offset;         /**< Read offset in current buffer */

    char *recvbuff;   /**< Buffer storage for lines of XML data */
    int max_len;      /**< Allocated length of recvbuff */
};

static void *start_file_read_thread(void *);

struct osm_planet *osm_planet_open(const char *filename)
{
    struct osm_planet *osf = calloc(1, sizeof(struct osm_planet));
    int bzerror;

    if( !(osf->fp = fopen(filename, "rb")))
    {
        fprintf(stderr, "osm_planet_open(): Unable to open file <%s>: %s\n", filename,
                strerror(errno));
        goto open_failed;
    }

    osf->bzfp = BZ2_bzReadOpen(&bzerror, osf->fp, 1, 0, NULL, 0);
    if(bzerror != BZ_OK)
    {
        fprintf(stderr, "osm_planet_open(): Unable to open compressed file with bzip2: %d\n", bzerror);
        goto open_failed;
    }

    pthread_mutex_init(&osf->drained_mutex, NULL);
    pthread_mutex_init(&osf->filled_mutex, NULL);
    pthread_cond_init(&osf->drained_signal, NULL);
    pthread_cond_init(&osf->filled_signal, NULL);

    if(pthread_create(&osf->file_read_thread, NULL, start_file_read_thread, osf) != 0)
    {
        fprintf(stderr, "osm_planet_open(): Unable to start file read thread\n");
        goto open_failed;
    }

    return osf;

open_failed:
    free(osf);
    return NULL;
}

int osm_planet_readln(struct osm_planet *osf, char **line)
{
    int len = 0;

    while(1)
    {
        char c;

        /* If the current buffer is empty, wait until it has been filled
         * by the decompressor running in a separate thread. */
        pthread_mutex_lock(&osf->filled_mutex);
        if( !osf->buff_filled[osf->curr_read_buff])
            pthread_cond_wait(&osf->filled_signal, &osf->filled_mutex);
        pthread_mutex_unlock(&osf->filled_mutex);

        /* Check if current buffer has just been fully drained... */
        if(osf->curr_read_offset == osf->buff_len[osf->curr_read_buff])
        {
            /* ...and if so, switch to other one, signal to the file
             * read thread that a new buffer is available for writing,
             * and continue. */
            osf->buff_filled[osf->curr_read_buff] = 0;
            osf->curr_read_buff = !osf->curr_read_buff;
            osf->curr_read_offset = 0;
            pthread_mutex_lock(&osf->drained_mutex);
            pthread_cond_signal(&osf->drained_signal);
            pthread_mutex_unlock(&osf->drained_mutex);
            continue;
        }

        /* Read the next character from the current buffer */
        c = osf->buff[osf->curr_read_buff][osf->curr_read_offset++];
       
        if(c == '\r' || c == '\n')
        {
            if(len == 0)
                /* Skip any trailing CR or LF characters from end of previous line
                 * (also conveniently skips blank lines) */
                continue;
            else
                /* Mark end of current line */
                c = '\0';
        }

        if(len >= osf->max_len)
        {
            osf->max_len += 10;
            osf->recvbuff = realloc(osf->recvbuff, osf->max_len);
        }
        osf->recvbuff[len] = c;
        if(c == '\0') /* Stop reading when we've got to the end of the current line */
            break;
        len++;
    }

    *line = osf->recvbuff;

    return 0;
}

int osm_planet_close(struct osm_planet *osf)
{
    int bzerror;

    /* Signal to file read thread and wait for it to exit */
    osf->exit_now = 1;
    pthread_join(osf->file_read_thread, NULL);
    pthread_detach(osf->file_read_thread);

    pthread_mutex_destroy(&osf->drained_mutex);
    pthread_mutex_destroy(&osf->filled_mutex);
    pthread_cond_destroy(&osf->drained_signal);
    pthread_cond_destroy(&osf->filled_signal);

    BZ2_bzReadClose(&bzerror, osf->bzfp);
    if(bzerror != BZ_OK)
    {
        fprintf(stderr, "osm_planet_close(): Error closing compressed file with bzip2: %d\n", bzerror);
        return 1;
    }

    if(fclose(osf->fp) != 0)
    {
        fprintf(stderr, "osm_planet_close(): Error closing file\n");
        return 1;
    }

    free(osf->recvbuff);
    free(osf);
    return 0;
}

/* Thread to read from the compressed file and perform bzip2 uncompression */
static void *start_file_read_thread(void *data)
{
    struct osm_planet *osf = data;
    unsigned char curr = 0;

    while(!osf->exit_now)
    {
        int bzerror;

        /* If there is no buffer available for writing, wait until the main
         * thread signals that it has just drained one of the buffers. */
        pthread_mutex_lock(&osf->drained_mutex);
        if(osf->buff_filled[curr] && !osf->exit_now)
            pthread_cond_wait(&osf->drained_signal, &osf->drained_mutex);
        pthread_mutex_unlock(&osf->drained_mutex);

        if(osf->exit_now)
            break;

        /* Decompress up to 900000 bytes into the current buffer and store the
         * number of bytes actually decoded, which may be less in the unlikely
         * event that the bzip2 compression hasn't been done with the maximum
         * block size (which is 900000 bytes). */
        osf->buff_len[curr] = BZ2_bzRead(&bzerror, osf->bzfp, osf->buff[curr], BLOCK_SIZE);

        /* Mark the current buffer as filled and signal to the main thread that
         * it is now available for reading. */
        osf->buff_filled[curr] = 1;
        curr = !curr;
        pthread_mutex_lock(&osf->filled_mutex);
        pthread_cond_signal(&osf->filled_signal);
        pthread_mutex_unlock(&osf->filled_mutex);

        if(bzerror == BZ_STREAM_END) /* end of compression block */
        {
            void *unused;
            int num_unused;

            /* If this is also end of file, then exit now... */
            if(feof(osf->fp))
            {
                fprintf(stderr, "End of file\n");
                break;
            }

            /* ...otherwise try reopening the stream to read the next block. */
            BZ2_bzReadGetUnused(&bzerror, osf->bzfp, &unused, &num_unused);
            fseek(osf->fp, -num_unused, SEEK_CUR);
            BZ2_bzReadClose(&bzerror, osf->bzfp);
            osf->bzfp = BZ2_bzReadOpen(&bzerror, osf->fp, 1, 0, NULL, 0);
        }

        if(bzerror != BZ_OK)
        {
            fprintf(stderr, "Error reading from compressed OSM file: %d\n", bzerror);
            break;
        }
    }

    fprintf(stderr, "File read thread exiting...\n");
    osf->finished = 1;
    return NULL;
}

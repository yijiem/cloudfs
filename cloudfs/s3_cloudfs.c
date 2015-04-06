#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include "cloudapi.h"
#include "cloudfs.h"

static char *my_bucket = "yijiem";
static FILE *infile;

int list_service(const char *bucket_name) {
    write_log("s3cloudfs: list service: %s....\n", bucket_name);
    return 0;
}

int list_bucket(const char *key, time_t modified_time, uint64_t size) {
    write_log("s3cloudfs: list bucket: %s %lu %llu\n", key, modified_time, size);
    return 0;
}

int put_buffer(char *buffer, int buffer_length) {
    write_log("s3cloudfs: put buffer length %d....\n", buffer_length);
    return fread(buffer, 1, buffer_length, infile);
}

char *get_key(const char *path) {
    char *res;
    int i;
    int length;

    length = strlen(path);
    res = (char *) malloc(length);
    for (i = 0; i < length; i++) {
        if (path[i] != '/') {
            res[i] = path[i];
        } else {
            res[i] = '+';
        }
    }

    write_log("s3cloudfs: get_key: key=%s....\n", res);
    return res;
}

int s3_init() {
    write_log("s3cloudfs: s3 init on host:%s....\n", state_.hostname);
    cloud_init(state_.hostname);
    cloud_print_error();
    return 0;
}

int s3_list_service() {
    write_log("s3cloudfs: s3 list service....\n");
    cloud_list_service(list_service);
    cloud_print_error();
    return 0;
}

int s3_list_bucket() {
    write_log("s3cloudfs: s3 list bucket....\n");
    cloud_list_bucket(my_bucket, list_bucket);
    cloud_print_error();
    return 0;
}

int s3_cloudfs_put(const char *path) {
    struct stat stat_buf;
    char *absolute_path;
    char *key;

    write_log("s3cloudfs: put objects....\n");
    absolute_path = get_absolute_path(path);
    key = get_key(path);
    infile = fopen(absolute_path, "rb");
    if (infile == NULL) {
        write_log("s3cloudfs: file not found!\n");
        free(absolute_path);
        free(key);
        return -1;
    }

    lstat(absolute_path, &stat_buf);
    cloud_put_object(my_bucket, key, stat_buf.st_size, put_buffer);
    fclose(infile);
    cloud_print_error();

    free(absolute_path);
    free(key);
    return 0;
}

int s3_cloudfs_close() {
    write_log("s3cloudfs: end....\n");
    cloud_destroy();
    return 0;
}

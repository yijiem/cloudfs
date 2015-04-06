#include <stdarg.h>
#include <fuse.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __CLOUDFS_H_
#define __CLOUDFS_H_

#define MAX_PATH_LEN 4096
#define MAX_HOSTNAME_LEN 1024

struct cloudfs_state {
  char ssd_path[MAX_PATH_LEN];
  char fuse_path[MAX_PATH_LEN];
  char hostname[MAX_HOSTNAME_LEN];
  int ssd_size;
  int threshold;
  int avg_seg_size;
  int rabin_window_size;
  char no_dedup;
};

static struct cloudfs_state state_;
static FILE *cloudfs_log;

int cloudfs_start(struct cloudfs_state* state,
                  const char* fuse_runtime_name);
void cloudfs_get_fullpath(const char *path, char *fullpath);
char *get_absolute_path(const char *path);
void write_log(const char *format, ...);
#endif

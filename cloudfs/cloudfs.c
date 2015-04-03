#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>
#include "cloudapi.h"
#include "cloudfs.h"
#include "dedup.h"

#define UNUSED __attribute__((unused))
#define MESSAGE_LENGTH 30 // single message length

static struct cloudfs_state state_;
static FILE *cloudfs_log;

void write_log(const char *message) {
  int res;
  int mes_length;
  int time_length;
  int total_length;
  time_t rawtime;
  struct tm *timeinfo;
  char *message_all;
  char *time_str;

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  time_str = asctime(timeinfo);
  mes_length = strlen(message);
  time_length = strlen(time_str);
  time_str[time_length-1] = ' '; // replace last \n with ' '
  total_length = mes_length + time_length;
  message_all = malloc(total_length);
  strcpy(message_all, time_str);
  strcat(message_all, message);
  res = fwrite(message_all, 1, total_length, cloudfs_log);
  if (res != total_length) {
    fprintf(stderr, "write_log() error...\n");
  }

  free(message_all);
}

#define DEBUG_CLOUDFS

/* @brief Debug function for cloudfs, if define DEBUG_CLOUDFS, this function will print
 *        debug infomation. Otherwise, it won't.
 * 
 * @param  func: pointer to the function name
 * @param  error_str: error message
 * @return retval: -errno
 */
#ifdef DEBUG_CLOUDFS
inline int cloudfs_error(const char *func, const char *error_str)
#else 
inline int cloudfs_error(const char *func UNUSED, const char *error_str UNUSED)
#endif
{
    int retval = -errno;

#ifdef DEBUG_CLOUDFS    
    /* Can change this line into log-styler debug function if necessary */
    fprintf(stderr, "CloudFS Error: Func[%s]:%s\n", func, error_str);
#endif

    return retval;
}


/* @brief Initializes the FUSE file system (cloudfs) by checking if the mount points
 *        are valid, and if all is well, it mounts the file system ready for usage.
 * 
 * @param  unused param
 * @return void
 */
static void *cloudfs_init(struct fuse_conn_info *conn UNUSED)
{
  int err;

  cloud_init(state_.hostname);

  cloudfs_log = fopen("/home/student/cloudfs/log", "w+");
  err = errno;
  if (err != 0) {
    fprintf(stderr, "create log fail! errno=%d\n", err);
  }
  write_log("create log success...\n");

  return NULL;
}

/* @brief Deinitialize S3. After this call is complete, no libs3 function may be
 *        called except S3_initialize().
 *
 * @param  unused param
 * @retrun void
 */
void cloudfs_destroy(void *data UNUSED) {
  cloud_destroy();
}

static int cloudfs_open(const char *path, struct fuse_file_info *fi) {
    int fd;

    fd = open(path, fi->flags);
    if (fd == -1) {
        write_log("open error!\n");
        return -errno;
    }
    write_log("open success...\n");
    write_log("path=\n");
    write_log(path);
    fi->fh = fd;
    return 0;
}

static int cloudfs_mkdir(const char *path, mode_t m) {
    int res;
    
    res = mkdir(path, m);
    if (res == -1) {
	write_log("mkdir error!\n");
        return -errno;
    }
    write_log("mkdir success...\n");
    return res;
}

static int cloudfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi) {
    (void) offset;
    (void) fi;

    if (strcmp(path, "/") != 0) {
        write_log("readdir fail!\n");
        return -ENOENT;
    }
    write_log("readdir success...\n");
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    return 0;
}

static int cloudfs_getattr(const char *path, struct stat *stbuf) {
    int res;

    res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        write_log("getattr success...\n");
        // stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else {
        write_log("getattr fail!\n");
        res = -ENOENT;
    }

    return res;
}

/*
 * Functions supported by cloudfs 
 */
static struct fuse_operations cloudfs_operations = {
    .init           = cloudfs_init,
    //
    // TODO
    //
    // This is where you add the VFS functions that your implementation of
    // MelangsFS will support, i.e. replace 'NULL' with 'melange_operation'
    // --- melange_getattr() and melange_init() show you what to do ...
    //
    // Different operations take different types of parameters. This list can
    // be found at the following URL:
    // --- http://fuse.sourceforge.net/doxygen/structfuse__operations.html
    //
    //
    .getattr	    = cloudfs_getattr,
    .mkdir          = cloudfs_mkdir,
    .open           = cloudfs_open,
    .readdir	    = cloudfs_readdir,
    .destroy        = cloudfs_destroy,
};

int cloudfs_start(struct cloudfs_state *state,
                  const char* fuse_runtime_name) {

  int argc = 0;
  char* argv[10];
  argv[argc] = (char *) malloc(128 * sizeof(char));
  strcpy(argv[argc++], fuse_runtime_name);
  argv[argc] = (char *) malloc(1024 * sizeof(char));
  strcpy(argv[argc++], state->fuse_path);
  argv[argc++] = "-s"; // set the fuse mode to single thread
  // argv[argc++] = "-f"; // run fuse in foreground 

  state_  = *state;

  printf("I am here\n");

  int fuse_stat = fuse_main(argc, argv, &cloudfs_operations, NULL);

  fclose(cloudfs_log);

  return fuse_stat;
}

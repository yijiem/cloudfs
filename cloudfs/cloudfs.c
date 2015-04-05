/*
 * you should never call mkdir(), or opendir() on fuse path
 * because these are used for the established file system like ext2(our ssd).
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#define S_IFDIR 0040000
#define S_IFREG 0100000
#define DEBUG 1

// static const char *hello_str = "Hello World!\n";
// static const char *fuse_path = "/fuse";
static struct cloudfs_state state_;
static FILE *cloudfs_log;

char *get_absolute_path(const char *path) {
    char *absolute_path;

    absolute_path = malloc(strlen(state_.fuse_path)+strlen(path));
    strcpy(absolute_path, state_.ssd_path); // do the trick
    strcat(absolute_path, path);

    return absolute_path;
}

void write_log(const char *format, ...) {
  int time_length, res;
  time_t rawtime;
  struct tm *timeinfo;
  char *time_str;

  va_list ap;
  va_start(ap, format);

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  time_str = asctime(timeinfo);
  time_length = strlen(time_str);
  time_str[time_length-1] = ' '; // replace last \n with ' '

  res = fwrite(time_str, 1, time_length, cloudfs_log);
  if (res != time_length) {
    fprintf(stderr, "write_log() error...\n");
  }
  vfprintf(cloudfs_log, format, ap);

  fflush(cloudfs_log);
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
    write_log("CloudFS Error: Func[%s]:%s\n", func, error_str);
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
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    fd = open(absolute_path, fi->flags);
    if (fd == -1) {
        write_log("open error!\n");
	free(absolute_path);
        return -errno;
    }
    write_log("open success...path=%s\n", path);
    fi->fh = fd;
    free(absolute_path);
    return 0;
}

static int cloudfs_mkdir(const char *path, mode_t m) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = mkdir(absolute_path, m);
    if (res == -1) {
	write_log("mkdir error! path=%s\n", absolute_path);
	free(absolute_path);
        return -errno;
    }
    write_log("mkdir success...path=%s\n", absolute_path);

    free(absolute_path);
    return res;
}

static int cloudfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi) {
    int retstat;
    DIR *dp;
    struct dirent *de;
    char *absolute_path;

    (void) offset;
    (void) fi;

    retstat = 0;
    absolute_path = get_absolute_path(path);
    write_log("readdir called.... path=%s\n", path);
    dp = (DIR *) (uintptr_t) fi->fh;
    if (dp == 0) {
        write_log("dp==0.... path=%s\n", absolute_path);
        dp = opendir(absolute_path);
    }
    if (DEBUG) write_log("DEBUG: dp=0x%08x\n", (int)dp);
    de = readdir(dp);
    if (de == 0) {
        write_log("readdir(dp) error! dp=0x%08x\n", (int)dp);
	free(absolute_path);
	return -errno;
    }

    do {
        write_log("calling filler with name %s\n", de->d_name);
	if (filler(buf, de->d_name, NULL, 0) != 0)
	    write_log("error not enough memory in filler!\n");
	    free(absolute_path);
	    return -ENOMEM;
    } while ((de = readdir(dp)) != NULL);

    free(absolute_path);
    return retstat;

/*
    if (strcmp(path, "/") != 0) {
        write_log("readdir fail! path=%s\n", path);
        return -ENOENT;
    }
    if (filler(buf, ".", NULL, 0) == 1) {
        write_log("error occurs when filler(1)! path=%s\n", path);
	return -ENOENT;
    }
    if (filler(buf, "..", NULL, 0) == 1) {
	write_log("error occurs when filler(2)! path=%s\n", path);
	return -ENOENT;
    }
    if (filler(buf, fuse_path + 1, NULL, 0) == 1) {
	write_log("error occurs when filler(3)! path=%s\n", path);
	return -ENOENT;
    }
    write_log("readdir success...path=%s\n", path);

    return 0;
*/
}

static int cloudfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char *absolute_path;

    res = 0;
    absolute_path = get_absolute_path(path);
    memset(stbuf, 0, sizeof(struct stat));
    if (stat(absolute_path, stbuf) != 0) {
	write_log("getattr fail! path=%s\n", absolute_path);
	free(absolute_path);
	return -errno;
    }
    write_log("getattr success.... path=%s\n", absolute_path);

/*
    if (strcmp(path, "/") == 0) {
	stat()
        write_log("getattr success....path=%s\n", path);
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    } else if (strcmp(path, fuse_path) == 0) {
        write_log("getattr success....path=%s\n", path);
        stbuf->st_mode = S_IFREG | 0444;
	stbuf->st_nlink = 1;
	stbuf->st_size = strlen(hello_str);
    } else {
        write_log("getattr fail!  path=%s\n", path);
        res = -ENOENT;
    }
*/
    free(absolute_path);
    return res;
}

static int cloudfs_utime(const char *path, struct utimbuf *buf) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = utime(absolute_path, buf);
    if (res == -1)
	return -errno;

    return 0;
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
    .utime	    = cloudfs_utime,
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

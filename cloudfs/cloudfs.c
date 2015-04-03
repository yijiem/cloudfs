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

static struct cloudfs_state state_;

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
void *cloudfs_init(struct fuse_conn_info *conn UNUSED)
{
    cloudfs_error(__FUNC__, "\n");
    cloud_init(state_.hostname);
    return NULL;
}

/* @brief Deinitialize S3. After this call is complete, no libs3 function may be
 *        called except S3_initialize().
 *
 * @param  unused param
 * @retrun void
 */
void cloudfs_destroy(void *data UNUSED) {
    cloudfs_error(__FUNC__, "\n");
    cloud_destroy();
}

int cloudfs_getattr(const char *path UNUSED, struct stat *statbuf UNUSED)
{
    cloudfs_error(__FUNC__, "\n");
    int retval = 0;
    return retval;
}

int cloudfs_mkdir(const char *path UNUSED, mode_t mode UNUSED){
    cloudfs_error(__FUNC__, "\n");
    return 0;
}

int cloudfs_open(const char *path UNUSED, struct fuse_file_info* fi UNUSED){
    cloudfs_error(__FUNC__, "\n");
    return 0;
}

/*
 * Functions supported by cloudfs 
 */
static struct fuse_operations cloudfs_operations = {
    .init           = cloudfs_init,
    .open           = cloudfs_open,
    .mkdir          = cloudfs_mkdir,
    .destroy        = cloudfs_destroy
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
  //argv[argc++] = "-f"; // run fuse in foreground 

  state_  = *state;

  int fuse_stat = fuse_main(argc, argv, &cloudfs_operations, NULL);
    
  return fuse_stat;
}

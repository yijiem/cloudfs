#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <getopt.h>
#include <unistd.h>
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
static void *cloudfs_init(struct fuse_conn_info *conn UNUSED)
{
  cloud_init(state_.hostname);

  printf("cloudfs_init()...\n");

  return NULL;
}

/* @brief Deinitialize S3. After this call is complete, no libs3 function may be
 *        called except S3_initialize().
 *
 * @param  unused param
 * @retrun void
 */
static void cloudfs_destroy(void *data UNUSED) {
    cloudfs_error(__func__, "wtf");
    cloud_destroy();
}

static int cloudfs_getattr(const char *path UNUSED, struct stat *statbuf UNUSED)
{
    cloudfs_error(__func__, "wtf");
    int retval = 0;
    return retval;
}

static int cloudfs_open(const char *path, struct fuse_file_info *fi) {
    int fd;
    
    printf("can you see this?\n");
    fd = open(path, fi->flags);
    if (fd == -1)
        return -errno;

    fi->fh = fd;
    return 0;
}

static int cloudfs_mkdir(const char *path, mode_t m) {
    int res;
    
    res = mkdir(path, m);
    if (res == -1)
        return -errno;
    
    return res;
}

static int cloudfs_rmdir(const char *path){
    int res;
    
    res = rmdir(path);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_unlink(const char *path){
    int res;

    res = unlink(path);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_symlink(const char *from, const char *to){
    int res;

    res = symlink(from, to);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_rename(const char *from, const char *to){
    int res;

    res = rename(from, to);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_link(const char *from, const char *to){
    int res;
    
    res = link(from, to);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_chmod(const char *path, mode_t mode){
    int res;
    
    res = chmod(path, mode);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_chown(const char *path, uid_t uid, gid_t gid){
    int res;
   // NOT SURE lchown or chown
    res = chown(path, uid, gid);
    if(res == -1)
	return -errno;
    return res;
}

static int cloudfs_truncate(const char *path, off_t len){
    int res;
  
    res = truncate(path, len);
    if(res == -1)
    	return -errno;
    return res;
}

static int cloudfs_ftruncate(const char *path, off_t len, struct fuse_file_info *fi){
    int res;
    (void) path;
   
    res = ftruncate(fi->fh, len);
    if(res == -1)
	return -errno;

    return 0;
}

//static int cloudfs_utimens(const char *path, const struct timespec ts[2]){
  //  int res;
//
  //  res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
    //if(res == -1)
//	return -errno;
//
  //  return res;
//}

//static int cloudfs_create(const char *path, mode_t mode, struct fuse_file_info *fi){
  //  int fd; 


/*
 * Functions supported by cloudfs 
 */
static struct fuse_operations cloudfs_operations = {
    .init           = cloudfs_init,
    .mkdir          = cloudfs_mkdir,
    .open           = cloudfs_open,
    .destroy        = cloudfs_destroy,
    .unlink         = cloudfs_unlink,
    .symlink        = cloudfs_symlink,
    .rename         = cloudfs_rename,
    .link           = cloudfs_link,
    .chmod          = cloudfs_chmod,
    .chown          = cloudfs_chown,
    .truncate       = cloudfs_truncate,
    .ftruncate      = cloudfs_ftruncate,
  //  .utimens        = cloudfs_utimes,
    .getattr        = cloudfs_getattr,
    .rmdir          = cloudfs_rmdir 
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
  // open("/home/student/log", )
  int fuse_stat = fuse_main(argc, argv, &cloudfs_operations, NULL);

  return fuse_stat;
}

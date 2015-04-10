/*
 * you should never call mkdir(), or opendir() on fuse path
 * because these are used for the established file system like ext2(our ssd).
 */

#define _XOPEN_SOURCE 500
#define AT_SYMLINK_NOFOLLOW 0x100

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
// #include <pthread.h>
#include "cloudapi.h"
#include "cloudfs.h"
#include "dedup.h"
#include "s3_cloudfs.h"

#define UNUSED __attribute__((unused))
#define MESSAGE_LENGTH 30 // single message length
#define DEBUG 1

// static const char *hello_str = "Hello World!\n";
// static const char *fuse_path = "/fuse";
struct cloudfs_state state_;
FILE *cloudfs_log;
char *proxy_suffix = ".proxy";

typedef struct meta_d {
    struct stat st;
}meta_d;

typedef struct file_handler {
    int fd;
    int remote_flag; // 0:local  1:remote
    char *tmp_file;
    uint8_t dirty;
    meta_d *md; // metadata field
}fh_t;

int is_remote(const char *path) {
    if (strstr(path, proxy_suffix) != NULL) {
        return 1;
    } else {
        return 0;
    }
}

int fh_t_initialize(fh_t *fh, const char *path, int fd, int rf) {
    write_log("I am all over it\n");
    fh->fd = fd;
    write_log("what?\n");
    fh->remote_flag = rf;
    if (rf == 0) {
	write_log("Sb call for an exterminator?\n");

	if (lstat(path, &(fh->md->st)) < 0) {
            write_log("fh_t_initialize: lstat fail!   path=%s\n", path);
	    return -errno;
        }

        write_log("I'm gone\n");
    } else {
	write_log("you are here?\n");

	if (read(fd, &(fh->md->st), sizeof(struct meta_d)) < 0) {
            write_log("fh_t_initialize: read meta_d into fh_t fail!   path=%s\n", path);
	    return -errno;
        }
    }
    return 0;
}

fh_t *fh_t_new() {
    fh_t *fh;

    fh = (fh_t *) malloc(sizeof(fh_t));
    fh->fd = 0;
    fh->tmp_file = NULL;
    fh->dirty = 0;
    fh->remote_flag = 0;
    fh->md = (meta_d *) malloc(sizeof(meta_d));

    return fh;
}

struct cloudfs_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

static inline DIR *get_dirp(const char *path, struct fuse_file_info *fi) {
    DIR *dp;

    dp = (DIR *) (uintptr_t) fi;
    if (dp == 0) {
        write_log("dp==0.... path=%s\n", path);
        dp = opendir(path);
    }

    return dp;
}

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
  cloudfs_log = fopen("/home/student/cloudfs/log", "w+");
  write_log("create log success...\n");
  s3_init();
  s3_list_service();

  return NULL;
}

/* @brief Deinitialize S3. After this call is complete, no libs3 function may be
 *        called except S3_initialize().
 *
 * @param  unused param
 * @retrun void
 */
void cloudfs_destroy(void *data UNUSED) {
    s3_cloudfs_close();
    fclose(cloudfs_log);
}

static int cloudfs_mknod(const char *path, mode_t mode, dev_t dev) {
    int res;
    char *absolute_path;

    res = 0;
    absolute_path = get_absolute_path(path);
    if (S_ISFIFO(mode)) {
        res = mkfifo(absolute_path, mode);
    } else {
        res = mknod(absolute_path, mode, dev);
    }
    if (res == -1) {
        write_log("mknod fail!  path=%s errno=%d\n", absolute_path, errno);
	free(absolute_path);
	return -errno;
    }

    write_log("mknod success.... path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

/*
static int cloudfs_create(const char *path, mode_t mode,
		struct fuse_file_info *fi) {
    int fd;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    if (fi->fh == 0) {
        // mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
        // fi->flags = O_CREAT | O_TRUNC | O_RDWR;
        fd = creat(path, mode);
	if (fd == -1) {
	    write_log("create fail!   path=%s errno=%d\n", absolute_path, errno);
	    free(absolute_path);
	    return -errno;
	}
	fi->fh = fd;
    }

    free(absolute_path);
    write_log("create success....path=%s\n", absolute_path);
    return 0;
}
*/

static int cloudfs_truncate(const char *path, off_t size) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = truncate(absolute_path, size);
    if (res == -1) {
	write_log("truncate fail!   path=%s\n", absolute_path);
	free(absolute_path);
	return -errno;
    }

    write_log("truncate success.... path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi) {
    int fd;
    ssize_t res;
    char *absolute_path;
    fh_t *fi_fh;

    fi_fh = (fh_t *) (uintptr_t) fi->fh;
    absolute_path = get_absolute_path(path);
    if (fi_fh->fd == 0) {
        fd = open(absolute_path, O_RDWR);
        if (fd == -1) {
	   write_log("write fail!   path=%s\n", absolute_path);
	   free(absolute_path);
    	   return -errno;
	}
        fh_t_initialize(fi_fh, absolute_path, fd, 0);
    } else {
        fd = fi_fh->fd;
    }

    res = pwrite(fd, buf, size, offset);
    if (res == -1) {
	write_log("write fail!   path=%s\n", absolute_path);
	res = -errno;
    } else {
	write_log("write success....path=%s\n", absolute_path);
    }

    free(absolute_path);
    return res;
}

static int cloudfs_open(const char *path, struct fuse_file_info *fi) {
    int fd;
    char *absolute_path;
    fh_t *fi_fh;
    int remote;

    remote = is_remote(path);

    fi_fh = fh_t_new();
    absolute_path = get_absolute_path(path);
    fd = open(absolute_path, fi->flags);
    if (fd == -1) {
        write_log("open error!   path=%s\n", absolute_path);
	free(absolute_path);
        free(fi_fh);
        return -errno;
    }
    write_log("I am here  remote=%d\n", remote);
    if (fh_t_initialize(fi_fh, absolute_path, fd, remote) < 0) { // do not change dirty bit
	write_log("open fail! fh_t_initialize error!   path=%s\n", absolute_path);
        free(absolute_path);
	close(fd);
	free(fi_fh);
	return -errno;
    }
    fi->fh = fi_fh;
    write_log("open success...path=%s\n", absolute_path);

    free(absolute_path);
    return 0;
}

static int cloudfs_read(const char *path, char *buf, size_t size, off_t offset,
			struct fuse_file_info *fi) {
    int res;
    char *absolute_path;
    fh_t *fi_fh;

    write_log("read called....\n");
    fi_fh = (fh_t *) (uintptr_t) fi->fh;

    // TODO: read into buffer from cloud file
    // TODO: read into fi_fh->tmp_file

    res = pread(fi_fh->fd, buf, size, offset);
    if (res == -1) {
        write_log("read fail!   fd=%d\n", fi_fh->fd);
        return -errno;
    }

    write_log("read success....\n");
    return res;
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
    write_log("readdir called.... path=%s\n", absolute_path);
    dp = get_dirp(absolute_path, fi);
    if (DEBUG) write_log("DEBUG: dp=0x%08x\n", (int)dp);
    de = readdir(dp);
    if (de == 0) {
        write_log("readdir(dp) error! dp=0x%08x\n", (int)dp);
	free(absolute_path);
	return -errno;
    }

    do {
        write_log("calling filler with name %s\n", de->d_name);
	if (filler(buf, de->d_name, NULL, 0) != 0) {
	    write_log("error not enough memory in filler!\n");
	    free(absolute_path);
	    return -ENOMEM;
	}
    } while ((de = readdir(dp)) != NULL);

    free(absolute_path);
    return retstat;

}

static int cloudfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char *absolute_path;

    res = 0;
    absolute_path = get_absolute_path(path);
    memset(stbuf, 0, sizeof(struct stat));
    if (lstat(absolute_path, stbuf) != 0) {
	write_log("getattr fail! path=%s\n", absolute_path);
	free(absolute_path);
	return -errno;
    }

    write_log("getattr success....path=%s\n", absolute_path);
    free(absolute_path);
    return res;
}

static int cloudfs_fgetattr(const char *path, struct stat *statbuf,
			struct fuse_file_info *fi) {
    fh_t *fi_fh;

    fi_fh = (fh_t *) (uintptr_t) fi;
    // TODO:
    write_log("fgetattr called....\n");
    statbuf = &(fi_fh->md->st);
    write_log("fgetattr success....\n");
    return 0;
}

/*
static int cloudfs_utime(const char *path, struct utimbuf *buf) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = utime(absolute_path, buf);
    if (res == -1)
	return -errno;

    return 0;
}
*/

static int cloudfs_getxattr(const char *path, const char *key,
			char *value, size_t size) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = lgetxattr(absolute_path, key, value, size);
    if (res == -1) {
        write_log("getxattr fail!   path=%s\n", absolute_path);
        free(absolute_path);
	return -errno;
    }

    write_log("getxattr success....path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_setxattr(const char *path, const char *key,
			const char *value, size_t size, int flags) {
    write_log("setxattr not implemented!\n");
    return 0;
}

static int cloudfs_listxattr(const char *path, char *list, size_t size) {
    write_log("listxattr not implemented!\n");
    return 0;
}

static int cloudfs_removexattr(const char *path, const char *name) {
    write_log("removexattr not implemented!\n");
    return 0;
}

static int cloudfs_rmdir(const char *path) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = rmdir(absolute_path);
    if (res == -1) {
        write_log("rmdir fail!   path=%s\n", absolute_path);
        free(absolute_path);
        return -errno;
    }

    write_log("rmdir success....path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_rename(const char *from, const char *to) {
    write_log("rename not implemented!\n");
    return 0;
}

static int cloudfs_flush(const char *path, struct fuse_file_info *file_info) {
    int res;
    fh_t *fi_fh;
    (void) path;

    fi_fh = (fh_t *) (uintptr_t) file_info->fh;
    res = close(dup(fi_fh->fd));
    if (res == -1) {
        write_log("flush fail!   fd=%d\n", (int) fi_fh->fd);
        return -errno;
    }

    return 0;
}

static int cloudfs_release(const char *path, struct fuse_file_info *file_info) {
    // struct stat stbuf;
    char *absolute_path;
    fh_t *fi_fh;

    write_log("calling release....\n");
    absolute_path = get_absolute_path(path);
    fi_fh = (fh_t *) (uintptr_t) file_info->fh;
    // lstat(absolute_path, &stbuf);
    if (fi_fh->md->st.st_size > state_.threshold) {
        int fd, res;
        ssize_t size;
        char *proxy_file;

        write_log("file: %s size is %d greater than 64KB\n	upload to cloud....\n", 
			absolute_path, (int) fi_fh->md->st.st_size);
        s3_cloudfs_put(path);


        // TODO: make local file as a proxy file
        res = unlink(absolute_path);
        if (res < 0) {
            write_log("release: unlink file fail!   path=%s\n", absolute_path);
	    free(absolute_path);
	    close(fi_fh->fd);
	    free(fi_fh);
	    return -errno;
        }
        proxy_file = malloc(strlen(absolute_path)+strlen(proxy_suffix));
        strcpy(proxy_file, absolute_path);
	strcat(proxy_file, proxy_suffix);
        fd = open(proxy_file, O_RDWR); // open same file in truncate mode
	if (fd < 0) {
            write_log("release: open proxy file fail!   path=%s\n",
			proxy_file);
	    free(absolute_path);
	    free(proxy_file);
            close(fi_fh->fd);
	    free(fi_fh);
	    return -errno;
        }
        size = write(fd, fi_fh->md, sizeof(meta_d));
	if (size < 0) {
            write_log("release: write metadata to proxy file fail!   path=%s\n",
			proxy_file);
	    free(absolute_path);
	    free(proxy_file);
            close(fi_fh->fd);
	    close(fd);
	    free(fi_fh);
	    return -errno;
        }
        close(fd);
        free(proxy_file);

    } else {
        write_log("file: %s size is %d smaller than 64KB\n	remain in SSD....\n",
			absolute_path, (int) fi_fh->md->st.st_size);
    }

    s3_list_service();
    s3_list_bucket();

    write_log("release success....\n");
    close(fi_fh->fd);
    free(absolute_path);
    free(fi_fh);
    return 0;
}

static int cloudfs_access(const char *path, int mask) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = access(absolute_path, mask);
    if (res == -1) {
        write_log("access fail!   path=%s\n", absolute_path);
        free(absolute_path);
        return -errno;
    }

    write_log("access success....path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_opendir(const char *path, struct fuse_file_info *file_info) {
    int res;
    char *absolute_path;

    res = 0;
    absolute_path = get_absolute_path(path);
    file_info->fh = (uint32_t) opendir(absolute_path);
    if (file_info->fh == 0) {
        write_log("opendir fail!   path=%s\n", absolute_path);
        res = -errno;
        free(absolute_path);
        return res;
    }

    write_log("opendir success.... path=%s\n", absolute_path);
    free(absolute_path);
    return res;
}

static int cloudfs_releasedir(const char *path, struct fuse_file_info *file_info) {
    // int res;
    // char *absolute_path;
    // DIR *dp;
    struct cloudfs_dirp *d = (struct cloudfs_dirp *) (uintptr_t) file_info->fh;
    (void) path;
    // absolute_path = get_absolute_path(path);
    // dp = get_dirp(absolute_path, file_info);
    closedir(d->dp);
    write_log("releasedir success....fi->fh=%d\n", file_info->fh);
    free(d);
    // free(absolute_path);
    return 0;
}

static int cloudfs_readlink(const char *path, char *buf, size_t bufsize) {
    write_log("readlink not implemented!\n");
    return 0;
}

static int cloudfs_unlink(const char *path) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = unlink(absolute_path);
    if (res == -1) {
        write_log("unlink fail!   path=%s\n", absolute_path);
        free(absolute_path);
        return -errno;
    }

    write_log("unlink success....path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_symlink(const char *path1, const char *path2) {
    write_log("symlink not implemented!\n");
    return 0;
}

static int cloudfs_link(const char *path1, const char *path2) {
    write_log("link not implemented!\n");
    return 0;
}

static int cloudfs_chmod(const char *path, mode_t mode) {
    write_log("chmod not implemented!\n");
    return 0;
}

static int cloudfs_chown(const char *path, uid_t owner, gid_t group) {
    write_log("chown not implemented!\n");
    return 0;
}

static int cloudfs_statfs(const char *path, struct statvfs *buf) {
    write_log("statfs not implemented!\n");
    return 0;
}

static int cloudfs_fsync(const char *path, int datasync,
			struct fuse_file_info *file_info) {
    write_log("fsync not implemented!\n");
    return 0;
}

static int cloudfs_utimens(const char *path, const struct timespec ts[2]) {
    int res;
    char *absolute_path;

    absolute_path = get_absolute_path(path);
    res = utimensat(0, absolute_path, ts, AT_SYMLINK_NOFOLLOW);
    if (res == -1) {
        write_log("utimens fail!   path=%s\n", absolute_path);
        free(absolute_path);
        return -errno;
    }

    write_log("utimens success....path=%s\n", absolute_path);
    free(absolute_path);
    return 0;
}

static int cloudfs_ftruncate(const char *path, off_t off,
			struct fuse_file_info *file_info) {
    write_log("ftruncate not implemented!\n");
    return 0;
}

/*
static int cloudfs_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi) {
    int res;

    (void) path;
    res = fstat(fi->fh, stbuf);
    if (res == -1) {
        write_log("fgetattr fail!   fi->fh=%d\n", fi->fh);
        return -errno;
    }

    write_log("fgetattr success....\n");
    return 0;
}
*/

/*
 * Functions supported by cloudfs
 */
static struct fuse_operations cloudfs_operations = {
    .init           = cloudfs_init,
    .destroy	    = cloudfs_destroy,
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
    .write	    = cloudfs_write,
    .read	    = cloudfs_read,
    // .create	    = cloudfs_create,
    .mknod	    = cloudfs_mknod,
    .truncate	    = cloudfs_truncate,
    .readdir	    = cloudfs_readdir,
    // .utime	    = cloudfs_utime,
    .getxattr	    = cloudfs_getxattr,
    .setxattr	    = cloudfs_setxattr,
    .listxattr	    = cloudfs_listxattr,
    .removexattr    = cloudfs_removexattr,
    .rmdir	    = cloudfs_rmdir,
    .rename	    = cloudfs_rename,
    .flush	    = cloudfs_flush,
    .release	    = cloudfs_release,
    .access	    = cloudfs_access,
    .opendir	    = cloudfs_opendir,
    // .releasedir	    = cloudfs_releasedir,
    .readlink	    = cloudfs_readlink,
    .unlink	    = cloudfs_unlink,
    .symlink	    = cloudfs_symlink,
    .link	    = cloudfs_link,
    .chmod	    = cloudfs_chmod,
    .chown          = cloudfs_chown,
    .statfs	    = cloudfs_statfs,
    .fsync	    = cloudfs_fsync,
    .utimens	    = cloudfs_utimens,
    .ftruncate	    = cloudfs_ftruncate,
    .fgetattr	    = cloudfs_fgetattr,
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

  int fuse_stat = fuse_main(argc, argv, &cloudfs_operations, NULL);

  return fuse_stat;
}

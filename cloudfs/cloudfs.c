/*
 * Use symbolic link and .metadata to creat the proxy file.
 * prevent getting file from cloud when doing ls -lr.
 * fetching file from cloud when opening, if it is just a proxy in local ssd.
 * create a temp file which just named as the original file name
 * during a open->read->write session.
 * Then, when release, checking if this file dirty or not,
 * if it is dirty and its size <= threshold, delete file on s3 and metadata
 * and save file in local ssd.
 * if it is dirty and its size > threshold, then flush to the cloud,
 * create(update) the metadata file(HOWTO? write struct stat to this file!),
 * delete the tmp file,
 * and make a symbolic link with the same name as the original file,
 * and link to the metadata file.
 * When getattr is called for this file, first check if it is a proxy in local,
 * if it is, read the symbolic link which is pointed to metadata to struct stat
 * then it is done!
 * this is really a beauty!
 */

#define _XOPEN_SOURCE 500
// #define AT_SYMLINK_NOFOLLOW 0x100
#define _ATFILE_SOURCE

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
#include <pthread.h>
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

/*
typedef struct meta_d {
    struct stat *st;
}meta_d;
*/

typedef struct file_handler {
    int fd;
    int proxy; // 0:local  1:remote
    uint8_t dirty;
    // meta_d *md; // metadata field
}fh_t;

/*
int fh_t_initialize(fh_t *fh, const char *path, int fd, int pf) {
    fh->fd = fd;
    fh->proxy_flag = pf;
    // TODO: maybe initialize tmp_file

    if (rf == 0) {
	if (lstat(path, fh->md->st) < 0) {
            write_log("fh_t_initialize: lstat fail!   path=%s\n", path);
	    return -errno;
        }
        write_log("do sth. forward...fi_fh->md->st->st_size=%d\n",
			fh->md->st->st_size);
    } else {
	if (read(fd, fh->md, sizeof(meta_d)) < 0) {
            write_log("fh_t_initialize: read meta_d into fh_t fail!   path=%s\n", path);
	    return -errno;
        }
    }

    return 0;
}
*/

fh_t *fh_t_new() {
    fh_t *fh;

    fh = (fh_t *) malloc(sizeof(fh_t));
    fh->fd = 0;
    fh->dirty = 0;
    fh->proxy = 0;

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

char *get_metadata_path(const char *path) {
    char *metadata_path;
    char *file_key;

    file_key = get_key(path);
    metadata_path = calloc(1, strlen(state_.metadata_path)+strlen(file_key)+5);
    strcpy(metadata_path, state_.metadata_path);
    strcat(metadata_path, "/");
    strcat(metadata_path, file_key);

    free(file_key);
    return metadata_path;
}

char *get_absolute_path(const char *path) {
    char *absolute_path;

    absolute_path = calloc(1, strlen(state_.ssd_path)+strlen(path)+5);
    strcpy(absolute_path, state_.ssd_path);
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

int mkdir_if_not_exist(const char *path) {
    int res;
    DIR *dir;

    res = 0;
    dir = opendir(path);
    if (!dir && errno == ENOENT) {
        res = mkdir(path, S_IRWXU | S_IRGRP | S_IXGRP);
    } else if (!dir) {
        res = -1;
    } else {
        res = closedir(dir);
    }

    return res;
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

  mkdir_if_not_exist(state_.metadata_path);

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

    write_log("mknod call....path=%s\n", path);

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

    write_log("truncate call....path=%s\n", path);

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

static int cloudfs_open(const char *path, struct fuse_file_info *fi) {
    int fd;
    char *absolute_path;
    char *metadata_path;
    fh_t *fi_fh;

    write_log("open call....path=%s\n", path);

    absolute_path = get_absolute_path(path);
    fi_fh = fh_t_new();
    metadata_path = get_metadata_path(path);
    if (access(metadata_path, F_OK) != -1) { // is proxy file
        fi_fh->proxy = 1;
        // get file from cloud
        write_log("proxy file on local....get from cloud and make tmp file....\n");
	if (unlink(absolute_path) < 0) { // remove previous symbolic link
	    write_log("unlink symbolic link file fail!\n");
	    free(absolute_path);
            free(metadata_path);
            free(fi_fh);
            return -errno;
        }
        s3_cloudfs_get(path);
    }

    fd = open(absolute_path, fi->flags);
    if (fd == -1) {
        write_log("open error!\n");
	free(absolute_path);
        free(metadata_path);
        free(fi_fh);
        return -errno;
    }
    fi_fh->fd = fd;
    fi->fh = (uint64_t) (uint32_t) fi_fh;

    write_log("open success....\n");
    free(absolute_path);
    free(metadata_path);
    return 0;
}

static int cloudfs_read(const char *path, char *buf, size_t size, off_t offset,
			struct fuse_file_info *fi) {
    int res;
    fh_t *fi_fh;

    write_log("read call....path=%s\n", path);

    fi_fh = (fh_t *) (uint32_t) fi->fh;
    res = pread(fi_fh->fd, buf, size, offset); // should be no difference
    if (res == -1) {
        write_log("read fail!   fd=%d\n", fi_fh->fd);
        return -errno;
    }

    write_log("read success....\n");
    return res;
}

static int cloudfs_write(const char *path, const char *buf, size_t size,
			off_t offset, struct fuse_file_info *fi) {
    int fd;
    ssize_t res;
    fh_t *fi_fh;

    write_log("write call....path=%s\n", path);

    fi_fh = (fh_t *) (uint32_t) fi->fh;
    if (fi_fh->proxy) { // proxy file
        fi_fh->dirty = 1; // set dirty bit
    }

    fd = fi_fh->fd;
    res = pwrite(fd, buf, size, offset);
    if (res == -1) {
	write_log("write fail!\n");
	res = -errno;
        return res;
    }

    write_log("write success....\n");
    return res;
}

static int cloudfs_release(const char *path, struct fuse_file_info *fi) {
    struct stat *stbuf;
    char *absolute_path;
    char *metadata_path;
    int fd, res;
    fh_t *fi_fh;

    write_log("calling release....path=%s\n", path);

    res = 0;
    fi_fh = (fh_t *) (uint32_t) fi->fh;
    absolute_path = get_absolute_path(path);
    metadata_path = get_metadata_path(path);
    stbuf = (struct stat *) malloc(sizeof(struct stat));
    if (lstat(absolute_path, stbuf) < 0) {
        write_log("get file stat(no matter tmp or permanent) fail!\n");
        res = -errno;
        goto FINISH;
    }
    if (fi_fh->proxy) {
        if (fi_fh->dirty) {
	    write_log("file is dirty....\n");
	    if (stbuf->st_size <= state_.threshold) {
		write_log("size smaller than threshold....delete file on cloud and metadata file....\n");
		s3_delete(path);
                unlink(metadata_path);
		// make temp file as the real file
	        // it's done, what? so simple, so elegant? YES!
		goto SUCCESS;
            } else {
                write_log("size still greater than threshold....flush to s3....\n");
                s3_cloudfs_put(path);
                fd = open(metadata_path, O_RDWR | O_TRUNC | O_CREAT,
	   			    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if (fd < 0) {
                    write_log("open and truncate old metadata file fail!\n");
                    res = -errno;
		    goto FINISH;
                }
                if (write(fd, stbuf, sizeof(struct stat)) < 0) {
                    write_log("write to metadata file fail!\n");
                    res = -errno;
                    close(fd);
                    goto FINISH;
                }
                close(fd);
            }
        }
	// do not dirty or finish dealing dirty
        // delete tmp file
        if (unlink(absolute_path) < 0) {
            write_log("unlink file fail!\n");
            res = -errno;
            goto FINISH;
        }
        if (symlink(metadata_path, absolute_path) < 0) {
            write_log("symlink fail!\n");
            res = -errno;
	    goto FINISH;
        }
    } else { // not proxy file
        if (stbuf->st_size > state_.threshold) {
            write_log("file: %s size is %d greater than 64KB\n	upload to cloud....\n", 
			absolute_path, stbuf->st_size);
            s3_cloudfs_put(path);
            fd = open(metadata_path, O_RDWR | O_TRUNC | O_CREAT,
				S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) {
                write_log("open new metadata file fail!\n");
                res = -errno;
		goto FINISH;
            }
            if (write(fd, stbuf, sizeof(struct stat)) < 0) {
                write_log("write to metadata file fail!\n");
                res = -errno;
                close(fd);
                goto FINISH;
            }
            close(fd);
            if (unlink(absolute_path) < 0) {
                write_log("unlink file fail!\n");
                res = -errno;
                goto FINISH;
            }
            if (symlink(metadata_path, absolute_path) < 0) {
                write_log("symlink fail!\n");
                res = -errno;
	        goto FINISH;
            }
        } else {
            write_log("file: %s size is %d smaller than 64KB\n	remain in SSD....\n",
			absolute_path, stbuf->st_size);
        }
    }
SUCCESS:
    write_log("release success....\n");
    s3_list_service();
    s3_list_bucket();

FINISH:
    close(fi_fh->fd);
    free(stbuf);
    free(absolute_path);
    free(metadata_path);
    free(fi_fh);
    return res;
}

static int cloudfs_getattr(const char *path, struct stat *stbuf) {
    int res, fd;
    char *absolute_path;
    char *metadata_path;

    write_log("getattr call....path=%s\n", path);

    res = 0;
    memset(stbuf, 0, sizeof(struct stat));
    absolute_path = get_absolute_path(path);
    metadata_path = get_metadata_path(path);
    write_log("metadata_path=%s\n", metadata_path);
    write_log("absolute_path=%s\n", absolute_path);

    if (lstat(absolute_path, stbuf) != 0) {
        write_log("lstat fail! errno=%d\n", errno);
	res = -errno;
        goto FINISH;
    }

    if (S_ISLNK(stbuf->st_mode)) {
        write_log("is symbolic link, path=%s\n", absolute_path);
	fd = open(absolute_path, O_RDONLY);
	if (fd < 0) {
	    write_log("open symbolic link fail!\n");
	    res = -errno;
	    goto FINISH;
	}
        memset(stbuf, 0, sizeof(struct stat));
	if (read(fd, stbuf, sizeof(struct stat)) < 0) {
            write_log("read metadata fail!\n");
	    res = -errno;
	    goto FINISH;
        }
	close(fd);
    } else {
	write_log("not symbolic link....\n");
    }

/*
    if (access(metadata_path, F_OK) != -1) { // is proxy file
	write_log("is proxy file....\n");
	fd = open(metadata_path, O_RDONLY);
        if (fd < 0) {
            write_log("open metadata_path fail!\n");
	    res = -errno;
            goto FINISH;
        }
	if (read(fd, stbuf, sizeof(struct stat)) < 0) {
            write_log("read metadata fail!\n");
	    res = -errno;
	    goto FINISH;
        }
        close(fd);
    } else {
	write_log("not proxy file....\n");
        if (lstat(absolute_path, stbuf) != 0) {
	    write_log("lstat fail! errno=%d\n", errno);
	    res = -errno;
            goto FINISH;
        }
    }
*/
    write_log("getattr success....\n");
    write_log("stbuf->st_size=%d\n", (int) stbuf->st_size);

FINISH:
    free(absolute_path);
    free(metadata_path);
    return res;
}


static int cloudfs_fgetattr(const char *path, struct stat *statbuf,
			struct fuse_file_info *fi) {
    fh_t *fi_fh;
    int res;

    write_log("fgetattr call....path=%s\n", path);

    res = 0;
    fi_fh = (fh_t *) (uint32_t) fi->fh;
    if (fstat(fi_fh->fd, statbuf) < 0) {
        res = -errno;
        write_log("fgetattr error!\n");
	return res;
    }

    write_log("fgetattr success....\n");
    return res;
}

static int cloudfs_mkdir(const char *path, mode_t m) {
    int res;
    char *absolute_path;

    write_log("mkdir call....path=%s\n", path);

    absolute_path = get_absolute_path(path);
    res = mkdir(absolute_path, m);
    if (res == -1) {
	write_log("mkdir error!\n");
	free(absolute_path);
        return -errno;
    }
    write_log("mkdir success....\n");

    free(absolute_path);
    return res;
}

static int cloudfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi) {
    int res;
    // DIR *dp;
    struct dirent *de;
    char *absolute_path;
    DIR *dp;
    (void) offset;

    write_log("readdir called.... path=%s\n", path);

    res = 0;
    absolute_path = get_absolute_path(path);

    // dp = get_dirp(absolute_path, fi);
    // if (DEBUG) write_log("DEBUG: dp=0x%08x\n", (int)dp);
    dp = (DIR *) (uint32_t) fi->fh;
    de = readdir(dp);
    if (de == 0) {
        write_log("readdir error! fi->fh=%d\n", (int) fi->fh);
	free(absolute_path);
	return -errno;
    }

    do {
        write_log("calling filler with name %s\n", de->d_name);
	if (strcmp(de->d_name, "lost+found") == 0) {
	    write_log("ignore lost+found....\n");
	    continue;
        }
	if (filler(buf, de->d_name, NULL, 0) != 0) {
	    write_log("error not enough memory in filler!\n");
	    free(absolute_path);
	    return -errno;
	}
    } while ((de = readdir(dp)) != NULL);

    free(absolute_path);
    return res;
}

static int cloudfs_opendir(const char *path, struct fuse_file_info *fi) {
    int res;
    char *absolute_path;

    write_log("opendir call....path=%s\n", path);
    res = 0;
    absolute_path = get_absolute_path(path);
    fi->fh = (uint32_t) opendir(absolute_path);
    if (fi->fh == 0) {
        write_log("opendir fail!   path=%s\n", absolute_path);
        res = -errno;
        free(absolute_path);
        return res;
    }

    write_log("opendir success.... path=%s\n", absolute_path);
    free(absolute_path);
    return res;
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

    write_log("getxattr call....path=%s\n", path);

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

static int cloudfs_setxattr(const char *path UNUSED, const char *key UNUSED,
			const char *value UNUSED, size_t size UNUSED, int flags UNUSED) {
    write_log("setxattr not implemented!\n");
    return 0;
}

static int cloudfs_listxattr(const char *path UNUSED, char *list UNUSED, size_t size UNUSED) {
    write_log("listxattr not implemented!\n");
    return 0;
}

static int cloudfs_removexattr(const char *path UNUSED, const char *name UNUSED) {
    write_log("removexattr not implemented!\n");
    return 0;
}

static int cloudfs_rmdir(const char *path UNUSED) {
    int res;
    char *absolute_path;

    write_log("rmdir call....path=%s\n", path);

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

static int cloudfs_rename(const char *from UNUSED, const char *to UNUSED) {
    write_log("rename not implemented!\n");
    return 0;
}

static int cloudfs_flush(const char *path, struct fuse_file_info *file_info) {
    int res;
    fh_t *fi_fh;
    (void) path;

    write_log("flush call....path=%s\n", path);
    fi_fh = (fh_t *) (uint32_t) file_info->fh;
    res = close(dup(fi_fh->fd));
    if (res == -1) {
        write_log("flush fail!   fd=%d\n", (int) fi_fh->fd);
        return -errno;
    }

    return 0;
}

static int cloudfs_access(const char *path, int mask) {
    int res;
    char *absolute_path;

    write_log("access call....path=%s\n", path);
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

static int cloudfs_releasedir(const char *path, struct fuse_file_info *fi) {
    DIR *dp;
    int res;

    write_log("releasedir call....path=%s\n", path);

    res = 0;
    dp = (DIR *) (uint32_t) fi->fh;
    if (closedir(dp) < 0) {
        write_log("releasedir fail!\n");
        res = -errno;
    }

    write_log("releasedir success....\n");
    return res;
}

static int cloudfs_readlink(const char *path UNUSED, char *buf UNUSED, size_t bufsize UNUSED) {
    write_log("readlink not implemented!\n");
    return 0;
}

static int cloudfs_unlink(const char *path) {
    int res;
    char *absolute_path;
    char *metadata_path;

    write_log("unlink call....path=%s\n", path);

    absolute_path = get_absolute_path(path);
    metadata_path = get_metadata_path(path);
    if (access(metadata_path, F_OK) != -1) { // is proxy file
        s3_delete(path);       // remove file on s3
	unlink(metadata_path); // remove metadata file
    }
    res = unlink(absolute_path);
    if (res == -1) {
        write_log("unlink fail!   path=%s\n", absolute_path);
        free(absolute_path);
        free(metadata_path);
        return -errno;
    }

    write_log("unlink success....path=%s\n", absolute_path);
    free(absolute_path);
    free(metadata_path);
    return 0;
}

static int cloudfs_symlink(const char *path1 UNUSED, const char *path2 UNUSED) {
    write_log("symlink not implemented!\n");
    return 0;
}

static int cloudfs_link(const char *path1 UNUSED, const char *path2 UNUSED) {
    write_log("link not implemented!\n");
    return 0;
}

static int cloudfs_chmod(const char *path UNUSED, mode_t mode UNUSED) {
    write_log("chmod not implemented!\n");
    return 0;
}

static int cloudfs_chown(const char *path UNUSED, uid_t owner UNUSED, gid_t group UNUSED) {
    write_log("chown not implemented!\n");
    return 0;
}

static int cloudfs_statfs(const char *path UNUSED, struct statvfs *buf UNUSED) {
    write_log("statfs not implemented!\n");
    return 0;
}

static int cloudfs_fsync(const char *path UNUSED, int datasync UNUSED,
			struct fuse_file_info *file_info UNUSED) {
    write_log("fsync not implemented!\n");
    return 0;
}

static int cloudfs_utimens(const char *path, const struct timespec ts[2]) {
    int res;
    char *absolute_path;

    write_log("utimens call....path=%s\n", path);
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

static int cloudfs_ftruncate(const char *path UNUSED, off_t off UNUSED,
			struct fuse_file_info *file_info UNUSED) {
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
void *worker(void *arg) {
    while (1) {
        char buf1[70];
	char buf2[70];
	char buf3[70];
	char buf4[70];
	char buf5[70];
	char buf6[70];
	char buf[500];

        scanf("%s", buf1);
	scanf("%s", buf2);

	scanf("%s", buf3);
	scanf("%s", buf4);
	scanf("%s", buf5);
	scanf("%s", buf6);

	strcpy(buf, buf1);
	strcat(buf, " ");
	strcat(buf, buf2);

	strcat(buf, " ");
	strcat(buf, buf3);
	strcat(buf, " ");
	strcat(buf, buf4);
	strcat(buf, " ");
	strcat(buf, buf5);
	strcat(buf, " ");
	strcat(buf, buf6);

        write_log("execute %s\n", buf);
        system(buf);
    }
    pthread_exit(NULL);
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
    .releasedir	    = cloudfs_releasedir,
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
  // pthread_t thread;

  argv[argc] = (char *) malloc(128 * sizeof(char));
  strcpy(argv[argc++], fuse_runtime_name);
  argv[argc] = (char *) malloc(1024 * sizeof(char));
  strcpy(argv[argc++], state->fuse_path);
  argv[argc++] = "-s"; // set the fuse mode to single thread
  // argv[argc++] = "-f"; // run fuse in foreground

  state_  = *state;

  // pthread_create(&thread, NULL, worker, NULL);

  int fuse_stat = fuse_main(argc, argv, &cloudfs_operations, NULL);

  // pthread_exit(NULL);
  return fuse_stat;
}

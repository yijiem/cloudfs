#include <sys/types.h>

extern char *my_bucket;
extern FILE *infile;

extern int s3_init();
extern int s3_list_service();
extern int s3_list_bucket();
extern int s3_cloudfs_put(const char *path);
extern int s3_cloudfs_close();

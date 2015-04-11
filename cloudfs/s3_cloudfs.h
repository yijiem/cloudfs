#include <sys/types.h>

extern const char *my_bucket;
// extern FILE *infile;
// extern FILE *outfile;

extern int s3_init();
extern int s3_list_service();
extern int s3_list_bucket();
extern int s3_cloudfs_put(const char *path);
extern int s3_cloudfs_get(const char *path);
extern int s3_cloudfs_close();

#include <sys/types.h>

static char *my_bucket = "yijiem";
static FILE *infile;

int s3_init();
int s3_list_service();
int s3_list_bucket();
int s3_cloudfs_put(const char *path);
int s3_cloudfs_close();

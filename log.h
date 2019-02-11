#include <stdio.h>
#include <errno.h>
#include <fuse.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <fuse.h>



int log_initialise (char* log_path);
int log_close (int fd);
int log_function (char* diskname, char* full_addr, char* format, ...);
int log_result (char* diskname, char* full_addr, char* name, int res, int is_error);
int log_server (char* diskname, char* server, char* full_addr, int sfd);
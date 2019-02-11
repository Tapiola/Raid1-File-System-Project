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
#include <pthread.h>

#include "log.h"

pthread_mutex_t mutex_in_log = PTHREAD_MUTEX_INITIALIZER;

int log_fd;

int log_initialise (char* log_path) 
{
	int fd = open (log_path, O_WRONLY);
	if (fd < 0)
		return -errno;
	log_fd = fd;
	return fd;
}

time_t log_t;

int log_time () 
{
	time (&log_t);
	struct tm t = *(struct tm*)localtime (&log_t);
	char resstr[100];
	char * format = "%a %d/%b/%y %T";

	strftime(resstr, sizeof(resstr), format, &t);
	dprintf (log_fd, "[%s] ", resstr);

	return 0;
}

int log_function (char* diskname, char* full_addr, char* format, ...) 
{
	pthread_mutex_lock (&mutex_in_log);
	log_time ();
	dprintf (log_fd, "%s %s ", diskname, full_addr);
	va_list ap;
	va_start (ap, format);
	vdprintf (log_fd, format, ap);	
	dprintf (log_fd, "\n");
	pthread_mutex_unlock (&mutex_in_log);
	return 0;
}

int log_result (char* diskname, char* full_addr, char* name, int res, int is_error) 
{
	pthread_mutex_lock (&mutex_in_log);
	log_time ();
	dprintf (log_fd, "%s %s ", diskname, full_addr);
	if (is_error == 0) {
		dprintf (log_fd, "%s returned %d", name, res);
	} else {
		dprintf (log_fd, "%s %s", name, strerror(errno));
	}
	dprintf (log_fd, "\n");
	pthread_mutex_unlock (&mutex_in_log);
	return 0;
}

int log_server (char* diskname, char* server, char* full_addr, int sfd) 
{
	pthread_mutex_lock (&mutex_in_log);
	log_time ();
	dprintf (log_fd, "%s %s with address %s at fd %d", diskname, server, full_addr, sfd);
	dprintf (log_fd, "\n");
	pthread_mutex_unlock (&mutex_in_log);
	return 0;
}

int log_close (int fd) 
{
	int res = close (fd);
	if (res < 0)
		return -errno;
	return res;	
}


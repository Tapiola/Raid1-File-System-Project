#define FUSE_USE_VERSION 30


#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>
#include <arpa/inet.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/md5.h>
#include <time.h>
#include "parser.h"
#include "log.h"
#include "cash.h"


int indx = -1;
int last_fd;

struct sockaddr_in addr_hs;
char * full_hs_addr;

int timeout;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_read = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER;
int swap_on;
char * diskname;

time_t start;
time_t now;

struct server_info {
	int sfd;
	struct sockaddr_in * addr;
	char* full_address;
};

struct server_info servers[2];

struct cqueue* cash_queue;


void rewriter (int receiver, int sender) {
	while (1) {
		size_t bytes;
		recv (sender, &bytes, sizeof(size_t), 0);
		send (receiver, &bytes, sizeof(size_t), 0);
		if (bytes == 0) 
			break;
		char buff[bytes];
		recv (sender, buff, bytes, 0);  
		send (receiver, buff, bytes, 0);   
	}
}

static void server_refill () 
{

	int size;
	recv (servers[0].sfd, &size, sizeof(int), 0);
	send (servers[1].sfd, &size, sizeof(int), 0);
	if (size == 0)
		return;

	int i = 0; for (i = 0; i < size; i++) {
		char d_name[100];
		recv (servers[0].sfd, d_name, 100, 0);
		send (servers[1].sfd, d_name, 100, 0);

		int is_dir;
		recv (servers[0].sfd, &is_dir, sizeof(int), 0);
		send (servers[1].sfd, &is_dir, sizeof(int), 0);	
		if (is_dir == 0) {
			rewriter (servers[1].sfd, servers[0].sfd);
		}

		server_refill ();
	}
}

static void exchange_servers () {
	int sfd_reserve = servers[0].sfd;
	memcpy (&servers[0].sfd, &servers[1].sfd, sizeof(int));
	memcpy (&servers[1].sfd, &sfd_reserve, sizeof(int));

	struct sockaddr_in addr_reserve;
	memcpy (&addr_reserve.sin_family, &servers[0].addr->sin_family, sizeof(sa_family_t));
	memcpy (&addr_reserve.sin_port, &servers[0].addr->sin_port, sizeof(in_port_t));
	memcpy (&addr_reserve.sin_addr, &servers[0].addr->sin_addr, sizeof(struct in_addr));

	memcpy (&servers[0].addr->sin_family, &servers[1].addr->sin_family, sizeof(sa_family_t));
	memcpy (&servers[0].addr->sin_port, &servers[1].addr->sin_port, sizeof(in_port_t));
	memcpy (&servers[0].addr->sin_addr, &servers[1].addr->sin_addr, sizeof(struct in_addr));

	memcpy (&servers[1].addr->sin_family, &addr_reserve.sin_family, sizeof(sa_family_t));
	memcpy (&servers[1].addr->sin_port, &addr_reserve.sin_port, sizeof(in_port_t));
	memcpy (&servers[1].addr->sin_addr, &addr_reserve.sin_addr, sizeof(struct in_addr));

	char* full_addr_reserve = servers[0].full_address;
	strcpy (servers[0].full_address, servers[1].full_address);
	strcpy (servers[1].full_address, full_addr_reserve);	
}


int cond;

void send_essentials (int indx, const char * path) 
{
	int server_main = 1;
	int rs1 = send (servers[0].sfd, &server_main, sizeof(int), MSG_NOSIGNAL);
	
	if (rs1 < 0) {
		exchange_servers ();
		send (servers[0].sfd, &server_main, sizeof(int), MSG_NOSIGNAL);
	}  

	server_main = 0;
	int rs2 = send (servers[1].sfd, &server_main, sizeof(int), MSG_NOSIGNAL);

	if (cond != 0) {
		time (&start);
	}
	if (rs2 < 0) {
		cond = 0;
		log_server (diskname, "server is disconnected", servers[1].full_address, servers[1].sfd);
		pthread_mutex_lock (&mutex);
		int passed = (int) difftime(time(&now), start);
		if (passed <= timeout) {
			close (servers[1].sfd);
			servers[1].sfd = socket (AF_INET, SOCK_STREAM, 0);
			int con = connect (servers[1].sfd, (struct sockaddr *) servers[1].addr, sizeof(struct sockaddr_in));
			if (con >= 0) {
				pthread_mutex_lock (&mutex_log);
				log_server (diskname, "server reconnected", servers[1].full_address, servers[1].sfd);
				pthread_mutex_unlock (&mutex_log);
				int srv_main = 0;
				send (servers[1].sfd, &srv_main, sizeof(int), MSG_NOSIGNAL);	
				swap_on = 1;
				cond = -1;						
			} 
		} else {
			close (servers[1].sfd);
			servers[1].sfd = socket (AF_INET, SOCK_STREAM, 0);
			memcpy (&servers[1].addr->sin_family, &addr_hs.sin_family, sizeof(sa_family_t));
			memcpy (&servers[1].addr->sin_port, &addr_hs.sin_port, sizeof(in_port_t));
			memcpy (&servers[1].addr->sin_addr, &addr_hs.sin_addr, sizeof(struct in_addr));
			strcpy (servers[1].full_address, full_hs_addr);

			int con = connect (servers[1].sfd, (struct sockaddr *) servers[1].addr, sizeof(struct sockaddr_in));
			if (con >= 0) {
				pthread_mutex_lock (&mutex_log);
				log_server (diskname, "hotswap connected", servers[1].full_address, servers[1].sfd);
				pthread_mutex_unlock (&mutex_log);
				int srv_main = 0;
				send (servers[1].sfd, &srv_main, sizeof(int), MSG_NOSIGNAL);
				swap_on = 1;	
				cond = -1;				
			}
		}
		pthread_mutex_unlock (&mutex);
	} 

	send (servers[0].sfd, &swap_on, sizeof(int), MSG_NOSIGNAL);
	send (servers[1].sfd, &swap_on, sizeof(int), MSG_NOSIGNAL);	
	if (swap_on == 1) {
		pthread_mutex_lock (&mutex);
		server_refill ();	
		pthread_mutex_unlock (&mutex);	
		swap_on = 0;			
	} 

	send (servers[0].sfd, &indx, sizeof(int), MSG_NOSIGNAL);
	send (servers[1].sfd, &indx, sizeof(int), MSG_NOSIGNAL);

	char path_new[PATH_MAX];
	strcpy (path_new, path);
	send (servers[0].sfd, path_new, PATH_MAX, MSG_NOSIGNAL);
	send (servers[1].sfd, path_new, PATH_MAX, MSG_NOSIGNAL);
}

static int net_getattr (const char * path, struct stat * stbuf)
{	
	int res = 0;
	send_essentials (0, path);

	struct stat stbuf2;
	recv (servers[0].sfd, &stbuf2, sizeof (struct stat), MSG_NOSIGNAL);
	memcpy (stbuf, &stbuf2, sizeof (struct stat));
	recv (servers[0].sfd, &res, sizeof(int), MSG_NOSIGNAL);

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "getattr: %s", path);
	pthread_mutex_unlock (&mutex_log);

	 int is_error = 1; 
	 if (res == 0) is_error = 0;
	 log_result (diskname, servers[0].full_address, "getattr:", res, is_error);

	return res;
}

static int net_opendir (const char *path, struct fuse_file_info * inf)
{	
	int res = 0;
	send_essentials (1, path);

	intptr_t fh;
	recv (servers[0].sfd, &fh, sizeof(intptr_t), 0);
	inf->fh = fh;

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "opendir: %s %d", path, (int)fh);
	pthread_mutex_unlock (&mutex_log);	

	recv (servers[0].sfd, &res, sizeof(int), 0);
	int is_error = 1; 
	if (res >= 0) is_error = 0;
	log_result (diskname, servers[0].full_address, "opendir:", res, is_error);

	if (res < 0) return -errno;
	return res;	
}


static int net_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
	off_t offset, struct fuse_file_info *inf) 
{
	int res = 0;
	send_essentials (2, path);

	send (servers[0].sfd, &offset, sizeof(off_t), MSG_NOSIGNAL);
	uintptr_t fh = (uintptr_t)inf->fh;
	send (servers[0].sfd, &fh, sizeof(uintptr_t), MSG_NOSIGNAL);

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "readdir: %s %d", path, (int)fh);
	pthread_mutex_unlock (&mutex_log);

	while (1) {
		char d_name[100];
		recv (servers[0].sfd, d_name, 100, 0);
		
		int full = filler(buf, d_name, NULL, 0);
		if (full != 0) 
			return -ENOMEM;
		int rd;
		recv (servers[0].sfd, &rd, sizeof(int), 0);
		if (rd == -1)
			break;
	}

	recv (servers[0].sfd, &res, sizeof(int), 0);
	int is_error = 1; 
	if (res >= 0) is_error = 0;
	log_result (diskname, servers[0].full_address, "readdir:", res, is_error);

	if (res < 0) return -errno;
	return res;	
}

int net_releasedir (const char *path, struct fuse_file_info *inf) 
{
	int res = 0;
	send_essentials (3, path);

	uintptr_t fh = (uintptr_t)inf->fh;
	send (servers[0].sfd, &fh, sizeof(uintptr_t), 0);	

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "releasedir: %s %d", path, (int)fh);
	pthread_mutex_unlock (&mutex_log);

	recv (servers[0].sfd, &res, sizeof(int), 0);
	int is_error = 1; 
	if (res >= 0) is_error = 0;
	log_result (diskname, servers[0].full_address, "releasedir:", res, is_error);
	
	if (res < 0) return -errno;
	return res;		
}

int net_mknod (const char *path, mode_t mode, dev_t dev) 
{
	int res = 0;
	send_essentials (4, path);
	int i;
	for (i = 0; i < 2; i++) {
		send (servers[i].sfd, &mode, sizeof(mode_t), 0);
		send (servers[i].sfd, &dev, sizeof(dev_t), 0);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "mknod: %s 0%o, %lld", path, mode, dev);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "mknod:", res, is_error);
	}
	
	if (res < 0) return -errno;
	return res;	
}

int net_mkdir (const char *path, mode_t mode) 
{
	int res = 0;
	send_essentials (5, path);
	int i;
	for (i = 0; i < 2; i++) {
		send (servers[i].sfd, &mode, sizeof(mode_t), 0);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "mkdir: %s 0%o", path);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "mkdir:", res, is_error);
	}
	
	if (res < 0) return -errno;
	return res;	
}

int net_rmdir (const char *path) 
{
	int res = 0;
	send_essentials (6, path);
	int i;
	for (i = 0; i < 2; i++) {
		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "rmdir: %s", path);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "rmdir:", res, is_error);
	}
	
	if (res < 0) return -errno;
	return res;			
}

int net_unlink (const char *path) 
{
	int res = 0;
	send_essentials (7, path);
	int i;
	for (i = 0; i < 2; i++) {
		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "unlink: %s", path);
		pthread_mutex_unlock (&mutex_log);

		if (i == 0) {
			delete_by_path (cash_queue, path);
		}

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "unlink:", res, is_error);
	}
	
	if (res < 0) return -errno;
	return res;	
}

int net_rename (const char *path, const char *newpath) 
{
	int res = 0;
	send_essentials (8, path);
	

	char path_rnm[PATH_MAX];
	strcpy(path_rnm, newpath);
	int i;
	for (i = 0; i < 2; i++) {
		send (servers[i].sfd, path_rnm, PATH_MAX, 0);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "rename: %s %s", path, newpath);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "rename:", res, is_error);
	}

	if (res < 0) return -errno;
	return res;	
}

int net_truncate (const char *path, off_t newsize) 
{
	send_essentials (9, path);

	send (servers[0].sfd, &newsize, sizeof(off_t), 0);
	send (servers[1].sfd, &newsize, sizeof(off_t), 0);

	log_function (diskname, servers[0].full_address, "truncate: %s %lld", path, newsize);
	log_function (diskname, servers[0].full_address, "truncate: %s %lld", path, newsize);

	delete_by_path (cash_queue, path);	

	int res1; int res2;
	recv (servers[0].sfd, &res1, sizeof(int), 0);
	recv (servers[1].sfd, &res2, sizeof(int), 0);

	if (res1 == res2 && res1 == 0) {
		log_result (diskname, servers[0].full_address, "write:", res1, 0);
		log_result (diskname, servers[1].full_address, "write:", res2, 0);
		return 0;
	}
	return res1;		
}

int net_open (const char *path, struct fuse_file_info * inf) 
{
	send_essentials (10, path);

	int flags = inf->flags;
	send (servers[0].sfd, &flags, sizeof(int), 0);
	send (servers[1].sfd, &flags, sizeof(int), 0);

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "open: %s %d", path, flags);
	log_function (diskname, servers[1].full_address, "open: %s %d", path, flags);
	pthread_mutex_unlock (&mutex_log);

	unsigned char hash_attr1[MD5_DIGEST_LENGTH];
	recv (servers[0].sfd, hash_attr1, MD5_DIGEST_LENGTH, 0);
	unsigned char hash_open1[MD5_DIGEST_LENGTH];
	recv (servers[0].sfd, hash_open1, MD5_DIGEST_LENGTH, 0);

	int comp1 = (int) memcmp(hash_attr1, hash_open1, MD5_DIGEST_LENGTH);

	unsigned char hash_attr2[MD5_DIGEST_LENGTH];
	recv (servers[1].sfd, hash_attr2, MD5_DIGEST_LENGTH, 0);
	unsigned char hash_open2[MD5_DIGEST_LENGTH];
	recv (servers[1].sfd, hash_open2, MD5_DIGEST_LENGTH, 0);

	int comp2 = (int) memcmp(hash_attr2, hash_open2, MD5_DIGEST_LENGTH);

	int comp3 = (int) memcmp(hash_open1, hash_open2, MD5_DIGEST_LENGTH);

	int rewrite = -1;
	if (comp3 != 0) {
		if (comp1 != 0 && comp2 == 0) {
			rewrite = 0;
			send (servers[0].sfd, &rewrite, sizeof(int), 0);
			send (servers[1].sfd, &rewrite, sizeof(int), 0);
			rewriter (servers[0].sfd, servers[1].sfd);
		} else if ((comp1 == 0 && comp2 != 0) || (comp1 == 0 && comp2 == 0)) {
			rewrite = 1;
			send (servers[0].sfd, &rewrite, sizeof(int), 0);
			send (servers[1].sfd, &rewrite, sizeof(int), 0);
			rewriter (servers[1].sfd, servers[0].sfd);
		} 
	} else {
		rewrite = -1;
		send (servers[0].sfd, &rewrite, sizeof(int), 0);
		send (servers[1].sfd, &rewrite, sizeof(int), 0);
	}

	int res1;
	int res2;
	recv (servers[0].sfd, &res1, sizeof(int), 0);
	inf->fh = res1;
	recv (servers[1].sfd, &res2, sizeof(int), 0);
	last_fd = res2;

	log_result (diskname, servers[0].full_address, "open:", res1, 0);
	log_result (diskname, servers[1].full_address, "open:", res2, 0);
	if (res1 >= 0 && res2 >= 0) 
		return 0;
	return -errno;		
}


int fat_send (int fd, void * buf, size_t size, int flag) {
	int data_recv = 0;
	while (data_recv < size) {
		int new_data = send (fd, buf + data_recv, size - data_recv, flag);
		data_recv += new_data;
	}
	return data_recv;
}

int fat_recv (int fd, void * buf, size_t size, int flag) {
	int data_recv = 0;
	while (data_recv < size) {
		int new_data = recv (fd, buf + data_recv, size - data_recv, flag);
		data_recv += new_data;
	}
	return data_recv;
}

int net_read (const char *path, char *buf, size_t size, off_t offset, 
	struct fuse_file_info *inf) 
{
	int res = 0;
	send_essentials (11, path);
	//pthread_mutex_lock (&mutex_read);
	uintptr_t fh = (uintptr_t)inf->fh;
	struct cnode * res_node = find_node (cash_queue, path, offset);

	send (servers[0].sfd, &size, sizeof(size_t), 0);
	send (servers[0].sfd, &offset, sizeof(off_t), 0);
	send (servers[0].sfd, &fh, sizeof(uintptr_t), 0);

	int is_cash;

	if (res_node != NULL) {
		is_cash = 1;
		send (servers[0].sfd, &is_cash, sizeof(int), 0);

		strcpy (buf, res_node->buf);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[0].full_address, "cash acquired from: %s %d %lld", 
											path, (int)size, offset);
		log_function (diskname, servers[0].full_address, "read: %s 0x%08x %d %lld %d", 
											path, buf, (int)size, offset, (int)fh);
		pthread_mutex_unlock (&mutex_log);	

		return size;	
	}
	else if (res_node == NULL) {
		is_cash = 0;
		send (servers[0].sfd, &is_cash, sizeof(int), 0);

		fat_recv (servers[0].sfd, buf, size, 0);

		add_front (cash_queue, path, buf, size, offset, fh);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[0].full_address, "cash added at: %s %d %lld", 
											path, (int)size, offset);		
		log_function (diskname, servers[0].full_address, "read: %s 0x%08x %d %lld %d", 
											path, buf, (int)size, offset, (int)fh);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[0].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[0].full_address, "read:", res, is_error);
		
		if (res < 0) return -errno;
	}
	return res;		
}


int net_write (const char *path, const char *buf, size_t size, off_t offset, 
	struct fuse_file_info *inf) 
{
	
	send_essentials (12, path);

	int i;
	for (i = 0; i < 2; i++) {	
		send (servers[i].sfd, &size, sizeof(size_t), MSG_NOSIGNAL);
		fat_send (servers[i].sfd, (char*)buf, size, MSG_NOSIGNAL);
		send (servers[i].sfd, &offset, sizeof(off_t), MSG_NOSIGNAL);

		uintptr_t fh;
		if (i == 0) fh = (uintptr_t)inf->fh;
		else if (i == 1) fh = (uintptr_t)last_fd;
		send (servers[i].sfd, &fh, sizeof(uintptr_t), MSG_NOSIGNAL);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[0].full_address, "write: %s 0x%08x %d %lld %d", 
											path, buf, (int)size, offset, (int) fh);
		pthread_mutex_unlock (&mutex_log);

		if (i == 0) {
			delete_by_path (cash_queue, path);
		}

	}
	int res1; int res2;
	recv (servers[0].sfd, &res1, sizeof(int), 0);
	recv (servers[1].sfd, &res2, sizeof(int), 0);

	if (res1 == res2 && res1 == 0) {
		log_result (diskname, servers[0].full_address, "write:", res1, 0);
		log_result (diskname, servers[1].full_address, "write:", res2, 0);
		return 0;
	}
	return res1;		
}



int net_release (const char *path, struct fuse_file_info *inf) 
{
	int res = 0;
	send_essentials (13, path);
	int i;
	for (i = 0; i < 2; i++) {
		uintptr_t fh;
		if (i == 0) fh = (uintptr_t)inf->fh;
		else if (i == 1) fh = (uintptr_t)last_fd;
		send (servers[i].sfd, &fh, sizeof(uintptr_t), 0);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "release: %s %d", path, (int)fh);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "release:", res, is_error);
	}

	if (res < 0) return -errno;
	return res;	

}

int net_chmod (const char *path, mode_t mode) 
{
	int res = 0;
	send_essentials (14, path);
	int i;
	for (i = 0; i < 2; i++) {
		send (servers[i].sfd, &mode, sizeof(mode_t), 0);

		pthread_mutex_lock (&mutex_log);
		log_function (diskname, servers[i].full_address, "chmod: %s 0%o", path, mode);
		pthread_mutex_unlock (&mutex_log);

		recv (servers[i].sfd, &res, sizeof(int), 0);
		int is_error = 1; 
		if (res >= 0) is_error = 0;
		log_result (diskname, servers[i].full_address, "chmod:", res, is_error);
	}

	if (res < 0) return -errno;
	return res;	
}

int net_utime (const char *path, struct utimbuf *ubuf) {
	int res = 0;
	send_essentials (15, path);

	pthread_mutex_lock (&mutex_log);
	log_function (diskname, servers[0].full_address, "utime: %s ", path);
	pthread_mutex_unlock (&mutex_log);

	recv (servers[0].sfd, &ubuf, sizeof(struct utimbuf), 0);
	recv (servers[0].sfd, &res, sizeof(int), 0);
	int is_error = 1; 
	if (res >= 0) is_error = 0;
	log_result (diskname, servers[0].full_address, "utime:", res, is_error);		
	
	if (res < 0) return -errno;
	return res;	
}


static struct fuse_operations net_oper = {
	.getattr = net_getattr, //0
	.opendir = net_opendir, //1
	.readdir = net_readdir, //2
	.releasedir = net_releasedir, //3
	.mknod = net_mknod, //4
	.mkdir = net_mkdir, //5
	.rmdir = net_rmdir, //6
	.unlink = net_unlink, //7
	.rename = net_rename, //8
	.truncate = net_truncate, //9
	.open = net_open, //10
	.read = net_read, //11
	.write = net_write, //12
	.release = net_release, //13
	.chmod = net_chmod, //14
	.utime = net_utime //15
};


int disk_main (int i, int argc, char* argv[]) {
	swap_on = 0;
	diskname = get_disks()[i]->diskname;
	
	const char del[2] = ":";
	struct server_info primary;
	struct server_info secondary;

	//server 1
	char * full_addr1 = get_disks()[i]->servers[0];
	primary.full_address =  malloc (strlen(full_addr1));
	strcpy (primary.full_address, full_addr1);
	char * tok1 = strtok(full_addr1, del);

	struct sockaddr_in addr1;
	int ip1;
	int sfd1 = socket(AF_INET, SOCK_STREAM, 0);
	inet_pton(AF_INET, tok1, &ip1);

	addr1.sin_family = AF_INET;
	tok1 = strtok(NULL, del);
	addr1.sin_port = htons(atoi(tok1));
	addr1.sin_addr.s_addr = ip1;

	primary.sfd = sfd1;
	primary.addr = &addr1;	

	int con1 = connect(primary.sfd, (struct sockaddr *) primary.addr, sizeof(struct sockaddr_in));
	if (con1 >= 0)
		log_server (diskname, "server connected", primary.full_address, primary.sfd);

	//server 2
	char * full_addr2 = get_disks()[i]->servers[1];
	secondary.full_address = malloc (strlen(full_addr2));
	strcpy (secondary.full_address, full_addr2);
	char * tok2 = strtok(full_addr2, del);

	struct sockaddr_in addr2;
	int ip2;
	int sfd2 = socket(AF_INET, SOCK_STREAM, 0);
	inet_pton(AF_INET, tok2, &ip2);

	addr2.sin_family = AF_INET;
	tok2 = strtok(NULL, del);
	addr2.sin_port = htons(atoi(tok2));
	addr2.sin_addr.s_addr = ip2;

	secondary.sfd = sfd2;
	secondary.addr = &addr2;	

	cond = -1;
	int con2 = connect(secondary.sfd, (struct sockaddr *) secondary.addr, sizeof(struct sockaddr_in));
	if (con2 >= 0)
		log_server (diskname, "server connected", secondary.full_address, secondary.sfd);

	//hotswap
	char * full_hs_addr_temp = get_disks()[i]->hotswap;
	full_hs_addr = malloc (strlen(full_hs_addr_temp));
	strcpy (full_hs_addr, full_hs_addr_temp);
	char * tok3 = strtok(full_hs_addr_temp, del);

	int ip3;
	inet_pton(AF_INET, tok3, &ip3);

	addr_hs.sin_family = AF_INET;
	tok3 = strtok(NULL, del);
	addr_hs.sin_port = htons(atoi(tok3));
	addr_hs.sin_addr.s_addr = ip3;

	servers[0] = primary; servers[1] = secondary;


	//set up fuse;
	argv[1] = malloc (3);
	strcpy (argv[1], "-f");
	argv[2] = malloc (3);
	strcpy (argv[2], "-s");
	size_t mountlen = strlen(get_disks()[i]->mountpoint);
	argv[3] = malloc (mountlen+1);
	strcpy (argv[3], get_disks()[i]->mountpoint);
	argc = 4;

	return fuse_main(argc, argv, &net_oper, &i);

	close(sfd1);
	close(sfd2);

	free(argv[1]);
	free(argv[2]);	
	free(argv[3]);		
}


int main (int argc, char* argv[])
{	
	configure (argv[1]);

	char* str = get_config()->cache_size;
	str[strlen(str)-1] = '\0';	
	int mb_size = atoi (str);

	cash_queue = init_queue (mb_size*1024*1024);
	
	timeout = atoi (get_config()->timeout);
	char* logfile = get_config()->errorlog;
	int log_fd = log_initialise (logfile);

	int child_status;	
	int i; 
	for (i = 0; i < DISK_COUNT; i++) {
		if (atoi(get_disks()[i]->raid) != 1) continue;
		switch(fork()) {
			case -1:
				exit(100);
			case 0:
				return disk_main (i, argc, argv);
				exit(0);
		}
	}
	
	wait (&child_status);
	log_close (log_fd);
	free_queue (cash_queue);

	return 0;	
}

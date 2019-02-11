#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/xattr.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include <limits.h>
#include <fuse.h>
#include <openssl/md5.h>
#include <sys/epoll.h>

#define BACKLOG 10

#define GETATTR 0
#define OPENDIR 1
#define READDIR 2
#define RELEASEDIR 3
#define MKNOD 4
#define MKDIR 5
#define RMDIR 6
#define UNLINK 7
#define RENAME 8
#define TRUNCATE 9
#define OPEN 10
#define READ 11
#define WRITE 12
#define RELEASE 13
#define CHMOD 14
#define UTIME 15


static int hash_file (const char* full, unsigned char hash[MD5_DIGEST_LENGTH]);
static int full_path (char full[PATH_MAX], char* storage, char* path);
static void rewriting (char * full, int rewr, int server_main);

static struct epoll_event ev;
int cfd;
int epfd;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static int hash_file (const char* full, unsigned char hash[MD5_DIGEST_LENGTH]) 
{
	MD5_CTX mdContext;
	int fd = open (full, O_CREAT | O_RDONLY);
	off_t read_offt = 0;
	size_t bytes; 
	MD5_Init (&mdContext);
	while (1) {
		char buff[4096];
		bytes = pread (fd, buff, 4096, read_offt);
		if (bytes == 0) {
			close (fd);
			break;
		}
		MD5_Update (&mdContext, buff, bytes);
		read_offt += 4096;
	}
	MD5_Final (hash, &mdContext);

	return 0;
}


static int full_path (char full[PATH_MAX], char* storage, char* path) 
{
	strcpy(full, storage);
	strncat(full, path, PATH_MAX);
	return 0;
}


int fat_send (int fd, char * buf, size_t size, int flag) {
	int data_recv = 0;
	while (data_recv < size) {
		int new_data = send (fd, buf + data_recv, size - data_recv, flag);
		data_recv += new_data;
	}
	return data_recv;
}

int fat_recv (int fd, char * buf, size_t size, int flag) {
	int data_recv = 0;
	while (data_recv < size) {
		int new_data = recv (fd, buf + data_recv, size - data_recv, flag);
		data_recv += new_data;
	}
	return data_recv;
}

void my_read(char * full) 
{
	int fd2 = open (full, O_RDONLY);
	off_t offt = 0;
	while (1) {
		char buff[4096];
		size_t bytes = pread (fd2, buff, 4096, offt); 
		send (cfd, &bytes, sizeof(size_t), 0);
		if (bytes == 0) {
			close (fd2);
			break;
		}
		fat_send (cfd, buff, bytes, 0); 
		offt += 4096;
	} 
}


void my_write(char * full) 
{
	int fd2 = open (full, O_TRUNC | O_WRONLY);
	off_t offt = 0;
	while (1) {
		size_t bytes;
		recv (cfd, &bytes, sizeof(size_t), 0);
		if (bytes == 0) {
			close (fd2);  
			break;
		}
		char buff[bytes];
		fat_recv (cfd, buff, bytes, 0); 
		pwrite (fd2, buff, bytes, offt);
		offt += bytes;
	}
}

static void rewriting (char * full, int rewr, int server_main) 
{
	if ((rewr == 0 && server_main == 0) || (rewr == 1 && server_main == 1)) {
		my_read(full);
	} else if ((rewr == 0 && server_main == 1) || (rewr == 1 && server_main == 0)) {
		my_write(full);	
	}
}

void path_for_hotswap (char new_path[PATH_MAX], char* path, char* d_name) {
	if (path[strlen(path)-1] != '/') {
		char path_path[PATH_MAX];
		full_path (path_path, (char*)path, "/");
		strcpy (path, path_path);
	}
	full_path (new_path, (char*)path, d_name);
}

static int is_directory (char* path) {
		struct stat path_stat;
		int rs = stat (path, &path_stat);	
		if (rs == -1)
			return -1;
		if (S_ISDIR(path_stat.st_mode) == 1)
			return 1;
		if (S_ISREG(path_stat.st_mode) == 1)
			return 0;
		return -1;
}


static void server_refill (char* path, int server_main) 
{
	if (server_main == 1) {

		if (is_directory (path) == 1) {

			DIR* fd_dir = opendir (path);
			char d_names[1024][100];

			int count = 0;
			struct dirent* read = readdir (fd_dir);
			if (read == 0)
				return;

			if (strcmp (read->d_name,".") != 0 && strcmp (read->d_name,"..") != 0) {
				strcpy (d_names[count], read->d_name);  
				count ++;
			}

			while (1) {
				read = readdir (fd_dir);			
				if (read == NULL) {
					send (cfd, &count, sizeof(int), 0);
					break;
				}
				if (strcmp (read->d_name,".") != 0 && strcmp (read->d_name,"..") != 0) {
					strcpy (d_names[count], read->d_name);  
					count ++;
				}
			}

			int i = 0; for (i = 0; i < count; i++) {
				char new_path[PATH_MAX];
				path_for_hotswap(new_path, path, d_names[i]);
				send (cfd, d_names[i], 100, 0);

				if (is_directory (new_path) == 1) {
					int is_dir = 1;
					send (cfd, &is_dir, sizeof(int), 0);    				
				} else if (is_directory (new_path) == 0) {
					int is_dir = 0;
					send (cfd, &is_dir, sizeof(int), 0);
					rewriting (new_path, 1, server_main);
				}

				server_refill (new_path, server_main);    
			}

			closedir (fd_dir);  
		}
		else if (is_directory (path) == 0) {
			int count = 0;
			send (cfd, &count, sizeof(int), 0);
			return;
		}
	}
	if (server_main == 0) {
		int size;
		recv (cfd, &size, sizeof(int), 0);  
		if (size == 0)
			return;

		int i = 0; for (i = 0; i < size; i++) {
			char d_name[100];
			recv (cfd, d_name, 100, 0);
			char new_path[PATH_MAX];
			path_for_hotswap(new_path, path, d_name);

			int is_dir;
			recv (cfd, &is_dir, sizeof(int), 0);
			if (is_dir == 1) {
				mkdir (new_path, 0777); 
			} else if (is_dir == 0) {
				mknod (new_path, S_IFREG | 0666, 0);
				rewriting(new_path, 1, server_main);
			}
			server_refill (new_path, server_main);

		}
	}
}


static int net_getattr (const char * path, struct stat * stbuf)
{
	int res = lstat (path, stbuf);
	if (res < 0) 
		return -errno;
	return res;
}

static int net_opendir (const char * path, intptr_t * fh)
{
	int res = 0;
	DIR * directory = opendir (path);
	if (directory == NULL)
		res = -errno;

	*(intptr_t*)fh = (intptr_t) directory;

	if (res < 0)
		return -errno;
	return res;
}

static int net_readdir (const char *path, off_t offset, uintptr_t fh)
{
	int res = 0;
	DIR * directory = (DIR *) fh;

	int rd = 0;
	struct dirent* read = readdir (directory);

	if (read == 0) {
		return -errno;
	}

	while (1) {
		send (cfd, read->d_name, 100, 0);

		read = readdir (directory);
		if (read == NULL) {
			rd = -1;
			send (cfd, &rd, sizeof(int), 0);
			break;
		}
		send (cfd, &rd, sizeof(int), 0);
	}

	if (res < 0)
		return -errno;
	return res;
}

int net_releasedir (const char *path, uintptr_t fh) 
{
	int res = 0;

	closedir((DIR *) (uintptr_t) fh);
	if (res < 0) 
		return -errno;
	return res;
}

int net_mknod (const char *path, mode_t mode, dev_t dev) 
{
	int res = 0;
	if (S_ISREG(mode)) {
		res = open (path, O_CREAT | O_EXCL | O_WRONLY, mode);   
		if (res >= 0) 
			res = close (res);
	} else if (S_ISFIFO(mode)) {
		res = mkfifo (path, mode);
	} else {
		res = mknod (path, mode, dev);
	}
	if (res < 0) 
		return -errno;
	return res;
}

int net_mkdir (const char *path, mode_t mode) 
{
	int res = mkdir (path, mode);   
	if (res < 0) 
		return -errno;
	return res;
}

int net_rmdir (const char *path) 
{
	int res = rmdir (path);
	if (res < 0) 
		return -errno;
	return res;
}

int net_unlink (const char *path) 
{
	int res = unlink (path);
	if (res < 0) 
		return -errno;  
	return res;
}

int net_rename (const char *path, const char *newpath) 
{
	int res = 0;

	res = rename (path, newpath);
	if (res < 0)
		return -errno;
	return res;
}

int net_truncate (const char *path, off_t newsize) 
{
	int res = 0;

	res = truncate(path, newsize);
	if (res < 0)
		return -errno;  
	return res;
}

int net_open (const char *path, int flags) 
{
	int fd = open (path, flags);
	if (fd < 0)
		return -errno;
	return fd;
}

int net_read (const char *path, char *buf, size_t size, 
	off_t offset, uintptr_t fh) 
{
	int res = pread (fh, buf, size, offset);
	
	if (res < 0)
		return -errno;
	return res;
}

int net_write (const char *path, const char *buf, size_t size, 
	off_t offset, uintptr_t fh) 
{
	int res = pwrite (fh, buf, size, offset);
	if (res < 0)
		return -errno;  
	return res;
}

int net_release (const char *path, uintptr_t fh) 
{
	int res = close (fh);
	if (res < 0)
		return -errno;
	return res;
}

int net_chmod (const char *path, mode_t mode) 
{
	int res = chmod (path, mode);
	if (res < 0)
		return -errno;  
	return res;

}

int net_utime (const char *path, struct utimbuf *ubuf) {
	int res = utime (path, ubuf);
	if (res < 0)
		return -errno;  
	return res;
}


int indx;

void client_handler (char* argv[]) 
{
	int data_size;

	while (1) {
		int server_main;
		recv (cfd, &server_main, sizeof(int), 0);
		int swap_on;
		recv (cfd, &swap_on, sizeof(int), 0);	
		if (swap_on == 1) {
			pthread_mutex_lock (&mutex);
			char path_cpy[PATH_MAX];
			strcpy(path_cpy, argv[3]);
			server_refill (path_cpy, server_main);
			pthread_mutex_unlock (&mutex);
		}
		

		recv (cfd, &indx, sizeof (int), 0);
		char path[PATH_MAX];
		data_size = recv (cfd, path, PATH_MAX, 0);
		if (data_size <= 0) {
			indx = -1;
			break;      
		}
		char full[PATH_MAX];
		full_path (full, argv[3], path);

		if (indx == GETATTR && server_main == 1) {        
			struct stat stbuf;

			int res = net_getattr (full, &stbuf);
			send (cfd, &stbuf, sizeof(struct stat), 0);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == OPENDIR && server_main == 1) {
			intptr_t fh;

			int res = net_opendir (full, &fh);
			send (cfd, &fh, sizeof(intptr_t), 0);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == READDIR && server_main == 1) {
			off_t offset;
			recv (cfd, &offset, sizeof(off_t), 0);
			uintptr_t fh;
			recv (cfd, &fh, sizeof(uintptr_t), 0);

			int res = net_readdir (full, offset, fh);
			send (cfd, &res, sizeof(int), 0);
		} 

		if (indx == RELEASEDIR && server_main == 1) {
			uintptr_t fh;
			recv (cfd, &fh, sizeof(uintptr_t), 0);

			int res = net_releasedir (full, fh);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == MKNOD) {
			mode_t mode;
			recv (cfd, &mode, sizeof(mode_t), 0);
			dev_t dev;
			recv (cfd, &dev, sizeof(dev_t), 0);
			int res = net_mknod (full, mode, dev);

			unsigned char hash[MD5_DIGEST_LENGTH];
			hash_file (full, hash);
			//int setx = 
			setxattr(full, "user.filehash", hash, MD5_DIGEST_LENGTH, 0);

			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == MKDIR) {
			mode_t mode;
			recv (cfd, &mode, sizeof(mode_t), 0);

			int res = net_mkdir (full, mode);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == RMDIR) {
			int res = net_rmdir (full);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == UNLINK) {
			int res = net_unlink (full);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == RENAME) {
			char path_rnm[PATH_MAX];
			recv (cfd, path_rnm, PATH_MAX, 0);
			char full_rnm[PATH_MAX];
			full_path (full_rnm, argv[3], path_rnm);

			int res = net_rename (full, full_rnm);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == TRUNCATE) {
			off_t newsize;
			recv (cfd, &newsize, sizeof(off_t), 0);

			int res = net_truncate (full, newsize);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == OPEN) {
			int flags;
			recv (cfd, &flags, sizeof(int), 0);

			unsigned char hash_res[MD5_DIGEST_LENGTH];
			hash_file (full, hash_res);

			unsigned char hash[MD5_DIGEST_LENGTH];
			getxattr(full, "user.filehash", hash, MD5_DIGEST_LENGTH);

			send (cfd, hash, MD5_DIGEST_LENGTH, 0);
			send (cfd, hash_res, MD5_DIGEST_LENGTH, 0);

			int rewr;
			recv (cfd, &rewr, sizeof(int), 0);

			rewriting (full, rewr, server_main);

			int res = net_open (full, flags);
			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == READ && server_main == 1) {
			size_t size;
			recv (cfd, &size, sizeof(size_t), 0);
			char buf[size];
			off_t offset;
			recv (cfd, &offset, sizeof(off_t), 0);
			uintptr_t fh;
			recv (cfd, &fh, sizeof(uintptr_t), 0);

			int is_cash;
			recv (cfd, &is_cash, sizeof(int), 0);

			if (is_cash == 0) {
				int res = net_read (full, buf, size, offset, fh);

				send (cfd, buf, size, 0);
				send (cfd, &res, sizeof(int), 0);
			}
		}

		if (indx == WRITE) {
			size_t size;
			recv (cfd, &size, sizeof(size_t), 0);
			char buf[size];
			recv (cfd, buf, size, 0);
			off_t offset;
			recv (cfd, &offset, sizeof(off_t), 0);
			uintptr_t fh;
			recv (cfd, &fh, sizeof(uintptr_t), 0);

			int res = net_write (full, buf, size, offset, fh);

			unsigned char hash[MD5_DIGEST_LENGTH];
			hash_file (full, hash); 
			setxattr(full, "user.filehash", hash, MD5_DIGEST_LENGTH, 0);

			send (cfd, &res, sizeof(int), 0);
		}

		if (indx == RELEASE) {
			uintptr_t fh;
			recv (cfd, &fh, sizeof(uintptr_t), 0);

			int res = net_release (full, fh);
			send (cfd, &res, sizeof(int), 0);
		}
		if (indx == CHMOD) {
			mode_t mode;
			recv (cfd, &mode, sizeof(mode_t), 0);

			int res = net_chmod (full, mode);
			send (cfd, &res, sizeof(int), 0);			
		}
		if (indx == UTIME && server_main == 1) {
			struct utimbuf ubuf;

			int res = net_utime (full, &ubuf);
			send (cfd, &ubuf, sizeof(struct utimbuf), 0);
			send (cfd, &res, sizeof(int), 0);
		}
	}

	close(cfd);
}




int main (int argc, char* argv[])
{
	epfd = epoll_create(2);

	int sfd;
	struct sockaddr_in addr;
	struct sockaddr_in peer_addr;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[2]));
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
	listen(sfd, BACKLOG);

	ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
	ev.data.fd = cfd;
	int res = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if (res < 0) return -errno;

	while (1) 
	{
		unsigned int peer_addr_size = sizeof(struct sockaddr_in);
		cfd = accept(sfd, (struct sockaddr *) &peer_addr, &peer_addr_size);

		int nfds = epoll_wait(epfd, &ev, 2, 10);
		if (nfds < 0) return -errno;

		client_handler(argv);
	}
	close(cfd);
	close(sfd);

	return 0;
}
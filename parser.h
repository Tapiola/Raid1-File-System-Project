
struct config 
{
	char* errorlog;
	char* cache_size;
	char* cache_replacment;
	char* timeout;
};

struct disk 
{
	char* diskname;
	char* mountpoint;
	char* raid;
	char** servers;
	int servers_size;
	char* hotswap;
};

int DISK_COUNT;

int configure(char* name);
struct config * get_config();
struct disk ** get_disks();
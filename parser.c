#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"

void tokenise_servers(char* str, const char* del, char** servers);

void fill_config_struct(char** config_details);
void fill_disk_struct (char** disk_details, struct disk ** disks, int j);

struct config conf;
struct disk * disks[10];


int configure (char* name) 
{
	FILE *fp = fopen(name, "r");
	char * buff = NULL;
	size_t len = 0;
	size_t read = 0;
	const char del[2] = "=";

	char * config_details[4];
	int i = 0; for (i = 0; i < 5; i++)
	{
		read = getline(&buff, &len, fp);
		if ((int)strlen(buff) == 1 || read == -1) 
		 	break;
		char * tok = strtok(buff, del);
		tok = strtok(NULL, del);
		int str_len = (int)strlen(tok);
		tok++;
		tok[str_len-2] = '\0';
		config_details[i] = malloc (200);
		strcpy(config_details[i], tok);
	}
	
	fill_config_struct(config_details);
	
	int j = 0;
	while(1)
	{
		if (read == -1) break;

		char* disk_details[5];
		
		int i = 0; for (i = 0; i < 6; i++)
		{
			read = getline(&buff, &len, fp);
			if ((int)strlen(buff) == 1 || read == -1) 
			 	break;
			char * tok = strtok(buff, del);
			tok = strtok(NULL, del);
			int str_len = (int)strlen(tok);
			tok++;
			tok[str_len-2] = '\0';
			disk_details[i] = malloc (300);
			strcpy (disk_details[i], tok);
		}
		
		fill_disk_struct(disk_details,disks,j);
		j++;
	}

	DISK_COUNT = j;

	fclose(fp);
	return 0;
}


void fill_config_struct(char** config_details) 
{
	conf.errorlog = config_details[0];
	conf.cache_size = config_details[1];
	conf.cache_replacment = config_details[2];
	conf.timeout = config_details[3];
}



void fill_disk_struct (char** disk_details, struct disk ** disks, int j) 
{
	struct disk dsk;
	dsk.diskname = disk_details[0];
	dsk.mountpoint = disk_details[1];
	dsk.raid = disk_details[2];

	const char del2[3] = ", ";
	int count = 0;
	char * ptr = disk_details[3];
	while((ptr = strchr(ptr, ',')) != NULL) 
	{
		count ++;
		ptr ++;
	}
	count ++;

	char * servers[count];

	char * tok = strtok(disk_details[3], del2);
	int i = 0; for (i = 0; i < count; i++)
	{
		servers[i] = malloc (100);
		strcpy (servers[i], tok);
		tok = strtok(NULL, del2);
	}
	dsk.servers = malloc(sizeof(servers));
	memcpy(dsk.servers,servers,sizeof(servers));
	dsk.servers_size = count;

	dsk.hotswap = disk_details[4];

	disks[j] = malloc(sizeof(struct disk));
	memcpy(disks[j],&dsk,sizeof(struct disk));	
}

struct disk ** get_disks() {
	return disks;
}

struct config * get_config() {
	return &conf;
}



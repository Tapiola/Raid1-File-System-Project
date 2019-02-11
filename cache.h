struct cnode 
{
	char* path; 
	char* buf; 
	size_t size; 
	off_t offset;
	int fd;
	struct cnode* prev;
	struct cnode* next;
};

struct cqueue 
{
	int count;
	size_t q_size;
	int capacity;
	struct cnode* head;
	struct cnode* tail;	
};

struct cnode* create_data (const char* path, char* buf, size_t size, off_t offset, int fd);
struct cqueue* init_queue (int capacity);
struct cnode* find_node (struct cqueue* queue, const char* path, off_t offset);
int add_front (struct cqueue* queue, const char* path, char* buf, size_t size, off_t offset, int fd);
int remove_rear (struct cqueue* queue);
int move_forward (struct cqueue* queue, struct cnode* node);
int delete_by_path (struct cqueue* queue, const char* path);
int free_queue (struct cqueue* queue);
int print_queue (struct cqueue* queue);
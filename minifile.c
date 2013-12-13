#include "minifile.h"
#include "disk.h"
#include "minithread.h"
#include "hashtable.h"
#include "synch.h"
#include <math.h>

hashtable_t pending_reads;
hashtable_t pending_writes;

void minifile_initialize(void) {
	pending_reads = hashtable_new(disk_size);
	pending_writes = hashtable_new(disk_size);
}

hashtable_t get_pending_reads(void) {
	return pending_reads;
}

hashtable_t get_pending_writes(void) {
	return pending_writes;
}

int protected_read(disk_t* disk, int blocknum, char* buffer) {
	char key[5];
	semaphore_t wait = semaphore_create();
	semaphore_initialize(wait,0);
	sprintf(key,"%d",blocknum);
	hashtable_put(pending_reads,key, wait);
	disk_read_block(disk, blocknum, buffer);
	semaphore_P(wait);
	hashtable_remove(pending_reads,key);
	semaphore_destroy(wait);
}

int protected_write(disk_t* disk, int blocknum, char* buffer) {
	char key[5];
	semaphore_t wait = semaphore_create();
	semaphore_initialize(wait,0);
	sprintf(key,"%d",blocknum);
	hashtable_put(pending_writes,key, wait);
	disk_write_block(disk, blocknum, buffer);
	semaphore_P(wait);
	hashtable_remove(pending_writes,key);
	semaphore_destroy(wait);
}

// returns the first token before the slash
char* get_first_token(char* string) {
	int i = 0;
	char* buffer = (char*) malloc(252);
	memset(buffer, 0, 252);
	while(string[i] != '\0' && string[i] != '/') {
		i++;
	}
	memcpy(buffer,string,i);
	buffer[i+1] = '\0';
	return buffer;
}


/* Make sure that path does not begin with slash */
inode_t resolve_absolute_path(char* path, inode_t cwd, int get_file, unsigned int* resolved_blocknum) {
	int i,j;
	inode_t resolved_path_inode = (inode_t) malloc(sizeof(struct inode));
	data_block_t temp_data_block = (data_block_t) malloc(sizeof(struct data_block));
	inode_t cwd_copy = (inode_t) malloc(sizeof(struct inode));

	if(cwd->data.size == 0 && strlen(path) > 0) {
		printf("[DEBUG] size is 0 \n");
		return NULL;
	} else if (cwd->data.size == 0 && strlen(path) == 0){
		return cwd;
	}

	printf("[DEBUG] resolve absolute path entered \n");
	memcpy(cwd_copy,cwd,sizeof(struct inode));
	//free(cwd);

	printf("[DEBUG] starting nested loops \n");
	// direct blocks
	for(i = 0; i < ceil((double)cwd->data.size / (DISK_BLOCK_SIZE / (sizeof(struct item)))); i++) {

		// directory data block contents (items)
		for(j = 0; j < DISK_BLOCK_SIZE / (sizeof(struct item)); j++) {
			// item (match name)
			if(strlen(path) == 0) {
				if(get_file == 1) {
					printf("[INFO] Could not resolve empty path \n");
					free(temp_data_block);
					free(cwd_copy);
					free(resolved_path_inode);
					return NULL;
				} else {
					free(temp_data_block);
					free(resolved_path_inode);
					return cwd_copy;
				}
			}
			else{
				printf("[DEBUG] path has not been parsed completely, trying to read temp data block \n");
				printf("direct block to read: %d, i = %d \n", cwd_copy->data.direct[i], i);
				protected_read(get_filesystem(),cwd_copy->data.direct[i],temp_data_block->padding);
				if(temp_data_block == NULL) {
					printf("temp data is null \n");
				}
				// item (match name)
				if(0 == strcmp(temp_data_block->dir_contents.items[j].name, get_first_token(path))) {
					printf("[DEBUG] match \n");
					protected_read(get_filesystem(),temp_data_block->dir_contents.items[j].blocknum,resolved_path_inode->padding);
					
					*resolved_blocknum = temp_data_block->dir_contents.items[j].blocknum;
					printf("[DEBUG] passing int blocknum : %d \n", *resolved_blocknum);
					resolved_path_inode = resolve_absolute_path(path += strlen(get_first_token(path)) + 1, resolved_path_inode, get_file, resolved_blocknum);
					//path += strlen(get_first_token(path)) + 1;
					//i = 0;
					free(temp_data_block);
					free(cwd_copy);
					return resolved_path_inode;
				} 
				else if (temp_data_block->dir_contents.items[j].blocknum ==  INT_MAX) {
					printf("[DEBUG] placeholder entry \n");
					continue;
				}
				else if (temp_data_block->dir_contents.items[j].blocknum == 0) {
					printf("[DEBUG] Path could not be resolved \n");
					free(temp_data_block);
					free(cwd_copy);
					free(resolved_path_inode);
					return NULL;
				}
				else {
					printf("[DEBUG] mismatch \n");
					continue;
				}
			}
		}
	}
	printf("[DEBUG] outside of for loop \n");
	return NULL;
}

inode_t resolve_relative_path(char* path, inode_t cwd, int get_file, unsigned int* resolved_blocknum) {
	int i,j;
	inode_t resolved_path_inode = (inode_t) malloc(sizeof(struct inode));
	data_block_t temp_data_block = (data_block_t) malloc(sizeof(struct data_block));
	inode_t cwd_copy = (inode_t) malloc(sizeof(struct inode));
	printf("[DEBUG] resolve relative path entered \n");
	printf("[DEBUG] cwd data size: %d, pathlen: %d \n", cwd->data.size, strlen(path));
	if(cwd->data.size == 0 && strlen(path) > 0) {
		printf("[DEBUG] size is 0 \n");
		return NULL;
	} else if (cwd->data.size == 0 && strlen(path) == 0){
		return cwd;
	}
	memcpy(cwd_copy,cwd,sizeof(struct inode));
	//free(cwd);

	// direct blocks
	for(i = 0; i < DISK_BLOCK_SIZE - sizeof(type_t) - sizeof(size_t) - sizeof(data_block_t); i++) {

		// directory data block contents (items)
		for(j = 0; j < DISK_BLOCK_SIZE / (sizeof(struct item)); j++) {
			// item (match name)
			printf("[DEBUG] inside nested loops, relative path \n");
			if(strlen(path) == 0) {
				printf("[DEBUG] path length is 0 \n");
				if(get_file == 1) {
					printf("[INFO] Could not resolve empty path \n");
					free(temp_data_block);
					free(cwd_copy);
					free(resolved_path_inode);
					return NULL;
				} else {
					free(temp_data_block);
					free(resolved_path_inode);
					return cwd_copy;
				}
			}
			else{
				printf("[DEBUG] path length is non-zero \n");
				protected_read(get_filesystem(),cwd->data.direct[i],temp_data_block->padding);
				printf("[DEBUG] read in inode from path token \n");
				// item (match name)
				if(0 == strcmp(temp_data_block->dir_contents.items[j].name, get_first_token(path))) {
					printf("[DEBUG] match \n");
					protected_read(get_filesystem(),temp_data_block->dir_contents.items[j].blocknum,resolved_path_inode->padding);
					
					*resolved_blocknum = temp_data_block->dir_contents.items[j].blocknum;
					printf("[DEBUG] passing int blocknum : %d \n", *resolved_blocknum);
					resolved_path_inode = resolve_absolute_path(path += strlen(get_first_token(path)) + 1, resolved_path_inode, get_file, resolved_blocknum);
					//path += strlen(get_first_token(path)) + 1;
					//i = 0;
					free(temp_data_block);
					free(cwd_copy);
					return resolved_path_inode;
				} 
				else if (temp_data_block->dir_contents.items[j].blocknum ==  1) {
					printf("[DEBUG] void entry \n");
					continue;
				}
				else if (temp_data_block->dir_contents.items[j].blocknum == 0) {
					printf("[DEBUG] Path could not be resolved \n");
					free(temp_data_block);
					free(cwd_copy);
					free(resolved_path_inode);
					return NULL;
				}
				else {
					printf("[DEBUG] mismatch \n");
					continue;
				}
			}
		}
	}

	// indirect
}

minifile_t minifile_creat(char *filename){
	printf("[DEBUG] Entered command: creat \n");
	return NULL;
}

minifile_t minifile_open(char *filename, char *mode){
	printf("[DEBUG] Entered command: open \n");
	return NULL;
}

int minifile_read(minifile_t file, char *data, int maxlen){
	printf("[DEBUG] Entered command: read \n");
	return -1;
}

int minifile_write(minifile_t file, char *data, int len){
	char* temp_data = (char*) malloc(4096);
	char key[5];
	semaphore_t wait;
	memset(temp_data, 0, 4096);
	sprintf(key, "%d", -1);
	sprintf(temp_data, "hahahaha");
	printf("[DEBUG] Entered command: write \n");
	disk_write_block(get_filesystem(), 8, temp_data);

	memset(temp_data, 0, 4096);
	sprintf(temp_data, "lololol");
	disk_write_block(get_filesystem(), -1, temp_data);

	protected_read(get_filesystem(), -1, temp_data);

	if(hashtable_get(pending_reads,key,(void**) &wait) != 0) {
		printf("[ERROR] wait semaphore for read was not in hashtable \n");
	}

	semaphore_P(wait);

	// temp_data is ready

	temp_data[30] = '\0';
	printf(temp_data);

	return 0;
}

int minifile_close(minifile_t file){
	printf("[DEBUG] Entered command: close \n");
	return -1;
}

int minifile_unlink(char *filename){
	printf("[DEBUG] Entered command: unlink \n");
	return -1;
}

int minifile_mkdir(char *dirname){
	int i, j;
	unsigned int temp = 2;
	int dir_made = 0;
	unsigned int free_block;
	unsigned int free_inode;
	char buffer[DISK_BLOCK_SIZE];
	inode_t parent_dir = get_current_working_directory();
	superblock_t super_block = get_superblock();
	printf("[DEBUG] got superblock \n");
	for(i = 0; i < strlen(dirname); i++) {
		if(dirname[i] == '/' || dirname[i] == '\\' || dirname[i] == ' ' || dirname[i] == '*') {
			printf("[ERROR] invalid directory name \n");
			return -1;
		}
	}
	printf("[DEBUG] resolving path, from curren dir: %p, blocknum: %d \n", get_current_working_directory(), get_current_blocknum());
	if(resolve_relative_path(dirname, get_current_working_directory(), 1, &temp) != NULL) {
		printf("[INFO] Directory name already exists \n");
		return -1;
	}
	printf("[DEBUG] path resolved \n");
	if(parent_dir->data.size == 0) {
		printf("[DEBUG] parent dir has no entries \n");
		free_block = super_block->data.next_free_data_block;
		protected_read(get_filesystem(), free_block, buffer);
		super_block->data.next_free_data_block = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		
		free_inode = super_block->data.next_free_inode;
		printf("[DEBUG] got free inode: %d \n", free_inode);
		protected_read(get_filesystem(), free_inode, buffer);
		super_block->data.next_free_inode = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		printf("[DEBUG] next free inode: %d \n", super_block->data.next_free_inode);
		printf("[DEBUG] superblock modified \n");
		protected_write(get_filesystem(), -1, super_block->padding);
		parent_dir->data.direct[0] = free_block;
		printf("[DEBUG] superblock written \n");
		memset(buffer, 0, DISK_BLOCK_SIZE);
		printf("[DEBUG] name of directory is %s \n", dirname);
		//memcpy(buffer, dirname, strlen(dirname) + 1);
		sprintf(((data_block_t) buffer)->dir_contents.items[0].name, dirname);
		((data_block_t) buffer)->dir_contents.items[0].blocknum = free_inode;
		printf("[DEBUG] free data block modified \n");
		protected_write(get_filesystem(), free_block, buffer);
		printf("[DEBUG] data block written \n");
		memset(buffer, 0, DISK_BLOCK_SIZE);

		((inode_t) buffer)->data.type = INODE_DIR;
		((inode_t) buffer)->data.size = 0;
		printf("[DEBUG] free inode modified \n");
		protected_write(get_filesystem(), free_inode, buffer);
		printf("[DEBUG] inode written \n");

		parent_dir->data.size++;
		protected_write(get_filesystem(), get_current_blocknum(), parent_dir->padding);
	} else {
		// if inode has data in it already
		i = 0;
		while(parent_dir->data.direct[i] != 0 && i < DISK_BLOCK_SIZE - sizeof(type_t) - sizeof(size_t) - 4) {

			protected_read(get_filesystem(), parent_dir->data.direct[i], buffer);

			for(j = 0; j < DISK_BLOCK_SIZE / (sizeof(struct item)); j++) {
				if(((data_block_t) buffer)->dir_contents.items[j].blocknum == 0 || ((data_block_t) buffer)->dir_contents.items[j].blocknum == INT_MAX) {
					memcpy(((data_block_t) buffer)->dir_contents.items[j].name, dirname, strlen(dirname) + 1);
					((data_block_t) buffer)->dir_contents.items[j].blocknum = super_block->data.next_free_inode;
					protected_write(get_filesystem(), parent_dir->data.direct[i], buffer);

					free_inode = super_block->data.next_free_inode;
					protected_read(get_filesystem(), free_inode, buffer);
					super_block->data.next_free_inode = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
					protected_write(get_filesystem(), -1, super_block->padding);

					memset(buffer, 0, DISK_BLOCK_SIZE);
					((inode_t) buffer)->data.size = 0;
					((inode_t) buffer)->data.type = INODE_DIR;
					protected_write(get_filesystem(), free_inode, buffer);

					parent_dir->data.size++;
					protected_write(get_filesystem(), get_current_blocknum(), parent_dir->padding);

					return 1;
				}
			}
			i++;
		}
	}
	return -1;
}

int minifile_rmdir(char *dirname){
	printf("[DEBUG] Entered command: rmdir \n");
	return -1;
}

int minifile_stat(char *path){
	printf("[DEBUG] Entered command: stat \n");
	return -1;
} 

int minifile_cd(char *path){
	unsigned int temp = get_current_blocknum();
	inode_t returned_node;
	if(path[0] == '/'){
		returned_node = resolve_absolute_path(path+1, get_root_directory(), 0, &temp);
	}
	else{
		returned_node = resolve_relative_path(path, get_current_working_directory(), 0, &temp);
	}
	if(returned_node == NULL) {
		printf("[INFO] directory not found \n");
		return -1;
	} else {
		set_current_blocknum(temp);
		set_current_working_directory(returned_node);
		printf("Current blocknum: %d, pointer: %p \n", get_current_blocknum(), get_current_working_directory());
		return 1;
	}
}

char **minifile_ls(char *path){
	inode_t returned_node;
	unsigned int temp = 2;
	char buffer[DISK_BLOCK_SIZE];
	char* *list;
	int block_iter = 0;
	int item_iter = 0;
	printf("[DEBUG] ls command entered \n");
	if(path[0] == '/'){
		returned_node = resolve_absolute_path(path+1, get_root_directory(), 0, &temp);
	}
	else{
		returned_node = resolve_relative_path(path, get_current_working_directory(), 0, &temp);
	}
	printf("[DEBUG] path resolved \n");
	if(returned_node == NULL){
		list = (char* *)malloc(sizeof(char*));
		list[0] = NULL;
		return list;
	}
	printf("[DEBUG] inode found, size: %d\n", returned_node->data.size);
	list = (char* *)malloc((returned_node->data.size + 1) * sizeof(char*));
	list[returned_node->data.size] = NULL;

	if(returned_node->data.size == 0){
		free(returned_node);
		return list;
	}
	printf("[DEBUG] reading entries from block: %d \n", block_iter);
	protected_read(get_filesystem(),returned_node->data.direct[block_iter],buffer);
	printf("[DEBUG] read direct block \n");
	block_iter++;
	while(((data_block_t)buffer)->dir_contents.items[item_iter].blocknum != 0){
		printf("[DEBUG] inside while \n");
		if(((data_block_t)buffer)->dir_contents.items[item_iter].blocknum != INT_MAX){
			printf("[DEBUG] inside if \n");
			list[item_iter] = (char*)malloc(252);
			memset(list[item_iter], 0, 252);
			memcpy(list[item_iter], ((data_block_t)buffer)->dir_contents.items[item_iter].name, strlen(((data_block_t)buffer)->dir_contents.items[item_iter].name));
			//printf("[DEBUG] item name: %s \n", ((data_block_t)buffer)->dir_contents.items[item_iter].name);
		}
		item_iter++;
		if(item_iter == DISK_BLOCK_SIZE / sizeof(struct item)){
			item_iter = 0;
			protected_read(get_filesystem(),returned_node->data.direct[block_iter],buffer);
			block_iter++;
		}
	}
	printf("[DEBUG] ls complete \n");
	free(returned_node);
	return list;
}

char* minifile_pwd(void){
	return get_current_path();
}

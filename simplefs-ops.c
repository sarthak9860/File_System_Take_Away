#include "simplefs-ops.h"

extern struct filehandle_t file_handle_array[MAX_OPEN_FILES]; // Array for storing opened files
struct inode_t inode;

int simplefs_create(char *filename) {
	for (int i = 0; i < 8; i++) {
		simplefs_readInode(i, &inode);
		if (strcmp(inode.name, filename) == 0)
			return -1;
	}

	int inode_number = simplefs_allocInode();
	if (inode_number == -1)
		return -1;

	struct inode_t new_inode;
	strcpy(new_inode.name, filename);
	new_inode.name[MAX_NAME_STRLEN - 1] = '\0';
	new_inode.status = INODE_IN_USE;
	new_inode.file_size = 0;
	for (int i = 0; i < MAX_FILE_SIZE; i++)
		new_inode.direct_blocks[i] = -1;

	simplefs_writeInode(inode_number, &new_inode);
	return inode_number;
}

void simplefs_delete(char *filename) {
	for (int i = 0; i < 8; i++) {
		simplefs_readInode(i, &inode);
		if (inode.status == INODE_IN_USE && strcmp(inode.name, filename) == 0) {
			for (int j = 0; j < MAX_FILE_SIZE; j++) {
				if (inode.direct_blocks[j] != -1) {
					simplefs_freeDataBlock(inode.direct_blocks[j]);
					inode.direct_blocks[j] = -1;
				}
			}
			simplefs_freeInode(i);
			return;
		}
	}
}

int simplefs_open(char *filename) {
	int found_inode = -1;
	for (int i = 0; i < 8; i++) {
		simplefs_readInode(i, &inode);
		if (inode.status == INODE_IN_USE && strcmp(inode.name, filename) == 0) {
			found_inode = i;
			break;
		}
	}
	if (found_inode == -1) {
		printf("Not found\n");
		return -1;
	}

	for (int i = 0; i < MAX_OPEN_FILES; i++) {
		if (file_handle_array[i].inode_number < 0) {
			file_handle_array[i].inode_number = found_inode;
			file_handle_array[i].offset = 0;
			return i;
		}
	}

	return -1;
}

void simplefs_close(int file_handle) {
	if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
		return;

	file_handle_array[file_handle].inode_number = -1;
	file_handle_array[file_handle].offset = 0;
}

int simplefs_read(int file_handle, char *buf, int nbytes) {
	if (file_handle < 0 || file_handle >= MAX_OPEN_FILES || nbytes < 0)
		return -1;

	int inode_number = file_handle_array[file_handle].inode_number;
	int offset = file_handle_array[file_handle].offset;

	if (inode_number == -1)
		return -1;

	simplefs_readInode(inode_number, &inode);
	if (offset + nbytes > inode.file_size)
		return -1;

	int bytes_read = 0;
	int current_offset = offset;

	while (bytes_read < nbytes) {
		int block_index = current_offset / BLOCKSIZE;
		int block_offset = current_offset % BLOCKSIZE;
		int block_num = inode.direct_blocks[block_index];

		if (block_num == -1)
			return -1;

		char temp_block[BLOCKSIZE];
		simplefs_readDataBlock(block_num, temp_block);

		int bytes_to_copy = BLOCKSIZE - block_offset;
		if (bytes_to_copy > (nbytes - bytes_read))
			bytes_to_copy = nbytes - bytes_read;

		memcpy(buf + bytes_read, temp_block + block_offset, bytes_to_copy);
		bytes_read += bytes_to_copy;
		current_offset += bytes_to_copy;
	}

	//file_handle_array[file_handle].offset = current_offset;
	return 0;
}

int simplefs_write(int file_handle, char *buf, int nbytes) {
	if (file_handle < 0 || file_handle >= MAX_OPEN_FILES || nbytes < 0)
		return -1;

	int inode_number = file_handle_array[file_handle].inode_number;
	int offset = file_handle_array[file_handle].offset;

	if (inode_number == -1 || (offset + nbytes) > (BLOCKSIZE * MAX_FILE_SIZE))
		return -1;

	simplefs_readInode(inode_number, &inode);

	int bytes_written = 0;
	int current_offset = offset;
	int newly_allocated[MAX_FILE_SIZE] = {0};

	while (bytes_written < nbytes) {
		int block_index = current_offset / BLOCKSIZE;
		int block_offset = current_offset % BLOCKSIZE;

		// Allocate block if not already allocated
		if (inode.direct_blocks[block_index] == -1) {
			int new_block = simplefs_allocDataBlock();
			if (new_block == -1) {
				for (int i = 0; i < MAX_FILE_SIZE; i++) {
					if (newly_allocated[i]) {
						simplefs_freeDataBlock(inode.direct_blocks[i]);
						inode.direct_blocks[i] = -1;
					}
				}
				return -1;
			}

			inode.direct_blocks[block_index] = new_block;
			newly_allocated[block_index] = 1;

			// Initialize the new block to zeros
			char zero_block[BLOCKSIZE];
			memset(zero_block, 0, BLOCKSIZE);
			simplefs_writeDataBlock(new_block, zero_block);
		}

		int block_num = inode.direct_blocks[block_index];
		char temp_block[BLOCKSIZE];
		simplefs_readDataBlock(block_num, temp_block);

		int space = BLOCKSIZE - block_offset;
		int to_copy = (nbytes - bytes_written < space) ? (nbytes - bytes_written) : space;

		memcpy(temp_block + block_offset, buf + bytes_written, to_copy);
		simplefs_writeDataBlock(block_num, temp_block);

		bytes_written += to_copy;
		current_offset += to_copy;
	}

	if (offset + nbytes > inode.file_size)
		inode.file_size = offset + nbytes;

	//file_handle_array[file_handle].offset = current_offset;
	simplefs_writeInode(inode_number, &inode);
	return 0;
}

int simplefs_seek(int file_handle, int nseek) {
	if (file_handle < 0 || file_handle >= MAX_OPEN_FILES)
		return -1;

	int inode_number = file_handle_array[file_handle].inode_number;
	if (inode_number == -1)
		return -1;

	simplefs_readInode(inode_number, &inode);
	int current_offset = file_handle_array[file_handle].offset;
	int new_offset = current_offset + nseek;

	if (new_offset < 0 || new_offset > inode.file_size)
		return -1;

	file_handle_array[file_handle].offset = new_offset;
	return 0;
}


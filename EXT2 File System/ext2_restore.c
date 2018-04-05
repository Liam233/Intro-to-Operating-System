#include "ext2_helper.h"

unsigned char *virtual_disk;

int main(int argc, char **argv){
	// Check arguments.
	if(argc != 3){
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk > <absolute path to a file or link>\n", argv[0]);
		exit(1);
	}

	int fd = open(argv[1], O_RDWR);
	virtual_disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	// mmap() error.
	if(virtual_disk == MAP_FAILED){
		perror("mmap");
		exit(1);
	}

	if(argv[2][0] != '/'){
		perror("Not an absolute path");
		exit(-1);
	}
	if(argv[2][strlen(argv[2]) - 1] == '/'){
		perror("Not a file.");
		exit(ENOENT);
	}

	struct ext2_super_block *sb = (struct ext2_super_block *) (virtual_disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (virtual_disk + EXT2_BLOCK_SIZE * 2);
	void *itable = virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	char *parent_name = get_parent_name(argv[2]);
	char *file_name = get_child_name(argv[2]);

	// Check existence of folder.
	int parent_node = get_directory_inode(virtual_disk, parent_name, itable);
	if(parent_node < 0){
		perror("Can't find parent directory.\n");
		exit(ENOENT);
	}

	// Check file inode.
	int file_node = get_file_inode(virtual_disk, parent_name, file_name, itable);
	if(file_node > 0){
		perror("File already exist.\n");
		exit(EEXIST);
	}

	struct ext2_inode *src_inode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (parent_node - 1));

	struct ext2_dir_entry *directory_entry;
	int i = 0;
	while(i < (src_inode->i_blocks / 2)){
		directory_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * src_inode->i_block[i]);
		int directory = 0;
		// Need to check all entries.
		while(directory < EXT2_BLOCK_SIZE){
			int required_space = directory_entry->name_len;
			// Required space needed to be rounded up to 4 bytes.
			if(required_space % 4){
				required_space = required_space + 4 - required_space % 4;
			}
			required_space = required_space + sizeof(struct ext2_dir_entry);
			int available_space = directory_entry->rec_len - required_space;
			directory = directory + required_space;
			// Check if there is enough space for deleted file.
			if(!available_space){
				directory_entry = (struct ext2_dir_entry *) ((char *) directory_entry + required_space);
				// Go to next entry.
				continue;
			}
			directory = directory + available_space;
			struct ext2_dir_entry *deleted_entry = (struct ext2_dir_entry *) ((char *) directory_entry + required_space);
			// Need to check all available space.
			while(0 < available_space){
				// Only check for file and link.
				if(deleted_entry->file_type & EXT2_FT_SYMLINK || deleted_entry->file_type & EXT2_FT_REG_FILE){
					if(!strncmp(deleted_entry->name, file_name, deleted_entry->name_len) && deleted_entry->name_len == strlen(file_name)){
						// Check if inode is repossessed.
						if(get_map(imap, deleted_entry->inode)){
							perror("Inode repossessed.\n");
							exit(ENOENT);
						}
						//Check if blocks is repossessed.
						struct ext2_inode *deleted_node = (struct ext2_inode *) (itable + (deleted_entry->inode - 1) * sizeof(struct ext2_inode));
						int block = 0;
						while(block < (deleted_node->i_blocks / 2) && block < 12){
							if(get_map(bmap, deleted_node->i_block[block])){
								perror("Block is repossessed.\n");
								exit(ENOENT);
							}
							block = block + 1;
						}
						// Update blocks.
						int i;
						for(i = 0; i < (deleted_node->i_blocks / 2); i ++){
							flip_map(bmap, deleted_node->i_block[i]);
							sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
							gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
						}
						// Update inode.
						flip_map(imap, deleted_entry->inode);
						sb->s_free_inodes_count = sb->s_free_inodes_count - 1;
						gd->bg_free_inodes_count = gd->bg_free_inodes_count - 1;

						deleted_entry->rec_len = available_space;
						directory_entry->rec_len = directory_entry->rec_len - available_space;
						deleted_node->i_dtime = 0;
						return 0;
					}
				}
				int deleted_length = deleted_entry->name_len;
				if(deleted_length % 4){
					deleted_length = deleted_length + 4 - deleted_length % 4;
				}
				deleted_length = deleted_length + sizeof(struct ext2_dir_entry);
				deleted_entry = (struct ext2_dir_entry *) ((char *) deleted_entry + deleted_length);
				available_space = available_space - deleted_length;
			}
		}
		i = i + 1;
	}
	perror("File cannot be found.\n");
	exit(ENOENT);
	return 0;
}

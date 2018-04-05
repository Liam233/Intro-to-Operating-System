#include "ext2_functions.h"

unsigned char *virtual_disk;

int main(int argc, char **argv){
	// Check arguments.
	if(argc != 2){
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	virtual_disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	// Check mmap error.
	if(virtual_disk == MAP_FAILED){
		perror("mmap");
		exit(1);
	}

	int fixes_count = 0;
	// Fix fix_count based on the bitmaps.
	// Superblock free the blocks.
	fixes_count = fixes_count + abs(fix_counters(virtual_disk, 1, 0));
	// Superblock free the inodes.
	fixes_count = fixes_count + abs(fix_counters(virtual_disk, 1, 1));
	// Block group free the blocks.
	fixes_count = fixes_count + abs(fix_counters(virtual_disk, 0, 0));
	// Block group free inodes.
	fixes_count = fixes_count + abs(fix_counters(virtual_disk, 0, 1));

	struct ext2_super_block *sb = (struct ext2_super_block *) (virtual_disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (virtual_disk + EXT2_BLOCK_SIZE * 2);
	void *itable = virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	// Offset from first non-reserved node.
	int offset = 0;
	// Starts at 2 for root node.
	int current_node = 2;
	do{
		struct ext2_inode *inode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (current_node - 1));
		if(inode->i_mode & EXT2_S_IFDIR && inode->i_size > 0){
			struct ext2_dir_entry *directory_entry;
			int i = 0;
			while(i < (inode->i_blocks / 2)){
				int directory = 0;
				directory_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * inode->i_block[i]);
				// Need to check all entries.
				while(directory < EXT2_BLOCK_SIZE){
					struct ext2_inode *entry_node = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (directory_entry->inode - 1));
					// Check file type and mode.
					int file_type;
					// file_type = 0 -> file.
					// file_type = 1 -> link.
					// file_type = 2 -> directory.
					for(file_type = 0; file_type < 3; file_type ++){
						fixes_count =  fixes_count + check_mode(entry_node, directory_entry, file_type);
					}
					// Check inode bitmaps.
					if(get_map(imap, directory_entry->inode) != 1){
						flip_map(imap, directory_entry->inode);
						sb->s_free_inodes_count = sb->s_free_inodes_count - 1;
						gd->bg_free_inodes_count = gd->bg_free_inodes_count - 1;
						fixes_count = fixes_count + 1;
						printf("Fixed: inode [%d] not marked as in-use\n", directory_entry->inode);
					}
					// Check inode's deletion time and reset.
					if(entry_node->i_dtime){
						entry_node->i_dtime = 0;
						fixes_count = fixes_count + 1;
						printf("Fixed: valid inode marked for deletion: [%d]\n", directory_entry->inode);
					}

					// Check data blocks are allocated in the data bitmaps.
					// Firstly, check directory block map.
					int blocks_fixes_count = 0;
					if(get_map(bmap, inode->i_block[i]) != 1){
						flip_map(bmap, inode->i_block[i]);
						sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
						gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
						fixes_count = fixes_count + 1;
						blocks_fixes_count = blocks_fixes_count + 1;
					}
					// Then check the File block.
					int file_count = 0;
					while(file_count < 12 && file_count < entry_node->i_blocks / 2){
						if(get_map(bmap, entry_node->i_block[i]) != 1){
							flip_map(bmap, entry_node->i_block[i]);
							sb->s_free_blocks_count = sb->s_free_blocks_count - 1;
							gd->bg_free_blocks_count = gd->bg_free_blocks_count - 1;
							blocks_fixes_count = blocks_fixes_count + 1;
						}
						file_count = file_count + 1;
					}

					if(blocks_fixes_count){
						fixes_count = fixes_count + blocks_fixes_count;
						printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", blocks_fixes_count, current_node);
					}
					directory = directory + directory_entry->rec_len;
					directory_entry = (struct ext2_dir_entry *) (directory_entry->rec_len + (char *) directory_entry);
				}
				i = i + 1;
			}
		}
		offset = offset + 1;
		current_node = offset + EXT2_GOOD_OLD_FIRST_INO;
	}while(current_node <= 32);

	if(fixes_count){
		printf("%d file system inconsistencies repaired!\n", fixes_count);
	}else{
		printf("No file system inconsistencies detected!\n");
	}
	return 0;
}

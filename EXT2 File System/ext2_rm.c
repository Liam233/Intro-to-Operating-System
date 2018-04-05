#include "ext2_helper.h"

unsigned char *virtual_disk;

int main(int argc, char **argv){
	// Check arguments.
	if(argc != 3){
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name> <absolute path>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	virtual_disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(virtual_disk == MAP_FAILED){
		perror("mmap");
		exit(1);
	}
	struct ext2_super_block *sb = (struct ext2_super_block *) (virtual_disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (virtual_disk + EXT2_BLOCK_SIZE * 2);
	void *itable = virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (virtual_disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	char *parent_name = get_parent_name(argv[2]);
	char *file_name = get_child_name(argv[2]);
	struct ext2_inode *file_node = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (check_file_path(virtual_disk,argv[2],itable) - 1));
	struct ext2_inode *parent_node = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (get_directory_inode(virtual_disk, parent_name, itable) - 1));

	struct ext2_dir_entry *directory_entry;
	struct ext2_dir_entry *padding_entry;
	int directory = 0;
	while(directory < EXT2_BLOCK_SIZE){
		// padding_entry should bypass the directory_entry.
		padding_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * parent_node->i_block[0] + directory);
		directory = directory + padding_entry->rec_len;
		directory_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * parent_node->i_block[0] + directory);

		if(EXT2_FT_REG_FILE & directory_entry->file_type){
			// Find the file with the same name.
			if(!strncmp(directory_entry->name, file_name, directory_entry->name_len) && directory_entry->name_len == strlen(file_name)){
				padding_entry->rec_len = padding_entry->rec_len + directory_entry->rec_len;
				// Set deletion time.
				file_node->i_dtime = (unsigned) time(NULL);
				flip_map(imap,directory_entry->inode);
				// Set free inode.
				sb->s_free_inodes_count = sb->s_free_inodes_count + 1;
				gd->bg_free_inodes_count = gd->bg_free_inodes_count + 1;

				// Update free_blocks_count.
				int i;
				for(i = 0; i < (file_node->i_blocks/2); i ++){
					flip_map(bmap, file_node->i_block[i]);
					sb->s_free_blocks_count = sb->s_free_blocks_count + 1;
					gd->bg_free_blocks_count = gd->bg_free_blocks_count + 1;
				}
				break;
			}
		}
	}
	return 0;
}

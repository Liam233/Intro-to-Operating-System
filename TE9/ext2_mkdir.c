#include "ext2_functions.h"

unsigned char *disk;

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name> <absolute path> \n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

	if (strlen(argv[2]) == 1 && argv[2][0] == '/') {
		perror("Root directory already exist!");
		exit(EEXIST);
	}

	char *dir_path = format_name(argv[2]);
	if (get_dir_inode(disk, dir_path, itable) != -1) {
		perror("New directory already exist!");
		exit(EEXIST);
	}

	char *owner_name = get_parent_name(dir_path);
	int dir_inode = get_dir_inode(disk, owner_name, itable);
	if (dir_inode < 0) {
		perror("Can't find parent directory.\n");
		exit(ENOENT);
	}

	unsigned char *imap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	int free_inode = get_free_inode(imap);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
	int free_block = get_free_block(bmap);

	//Make new Inode
	struct ext2_inode *new_inode = itable + (free_inode - 1) * sizeof(struct ext2_inode);
	new_inode->i_mode |= EXT2_S_IFDIR;
	new_inode->i_blocks = 2;
	new_inode->i_block[0] = free_block;
	new_inode->i_links_count = 0;
	new_inode->i_size = EXT2_BLOCK_SIZE;
	sb->s_free_inodes_count--;
	gd->bg_free_inodes_count--;
	flip_map(imap, free_inode);
	sb->s_free_blocks_count--;
	gd->bg_free_blocks_count--;
	flip_map(bmap, free_block);

	//Make new directory
	struct ext2_dir_entry *new_dir = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * free_block);
	new_dir->file_type = EXT2_FT_DIR;
	new_dir->inode = free_inode;
	new_dir->name_len = 1;
	new_dir->rec_len = 12;
	new_dir->name[0] = '.';

	//Parent directory ..
	new_dir = (struct ext2_dir_entry *) ((char *) new_dir + 12);
	new_dir->file_type |= EXT2_FT_DIR;
	new_dir->name_len = 2;
	new_dir->rec_len = EXT2_BLOCK_SIZE - 12;
	strcpy(new_dir->name, "..");

	char *child_name = get_child_name(dir_path);
	struct ext2_inode *inode = (struct ext2_inode *) (itable + (dir_inode - 1) * sizeof(struct ext2_inode));
	attach_child_to_parent(disk, inode, free_inode, EXT2_FT_DIR, child_name, bmap);

	gd->bg_used_dirs_count++;
	return 0;
}

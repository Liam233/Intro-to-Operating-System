#include "ext2_functions.h"

unsigned char *disk;

int main(int argc, char **argv) {
	if (argc != 4) {
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name> <source> <destination> \n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap\n");
		exit(1);
	}
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

	struct stat buf;    //Check file exists
	if (stat(argv[2], &buf)) {
		perror("File does not exist!\n");
		exit(ENOENT);
	}

	char *src_path = format_name(argv[3]);
	int folder_inode = get_dir_inode(disk, src_path, itable);    //Check destination exists
	if (folder_inode < 0) {
		perror("Can't find parent directory.\n");
		exit(ENOENT);
	}

	char *fname = get_child_name(argv[2]);    //Check destination exists
	if (get_file_inode(disk, src_path, argv[2], itable) > 0) {
		perror("Destination file already exists.\n");
		exit(EEXIST);
	}

	int size = buf.st_size;     //File size in bytes
	int num_blocks = 1;
	while (num_blocks * EXT2_BLOCK_SIZE < size) {       //Total amount of blocks for file
		num_blocks++;
	}
	if (gd->bg_free_blocks_count <= num_blocks) {
		perror("Not enough free blocks for file.\n");
		exit(ENOENT);
	}

	unsigned char *imap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	int free_inode = get_free_inode(imap);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	//Make new Inode
	struct ext2_inode *fnode = itable + sizeof(struct ext2_inode) * (free_inode - 1);
	fnode->i_mode |= EXT2_S_IFREG;
	fnode->i_size = size;
	fnode->i_blocks = 2 * num_blocks;
	fnode->i_dtime = 0;
	fnode->i_gid = 0;
	fnode->i_links_count = 1;
	fnode->i_ctime = (unsigned) time(NULL);
	fnode->i_atime = (unsigned) time(NULL);
	fnode->i_mtime = (unsigned) time(NULL);
	sb->s_free_inodes_count--;
	gd->bg_free_inodes_count--;
	flip_map(imap, free_inode);

	//read file data
	unsigned char *file_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, open(argv[2], O_RDWR), 0);
	if (file_data == MAP_FAILED) {
		perror("mmap\n");
		exit(1);
	}
	//copy all to blocks
	int copied = 0;
	int i;
	for (i = 0; i < num_blocks; i++) {
		int file_block = get_free_block(bmap);
		fnode->i_block[i] = file_block;
		gd->bg_free_blocks_count--;
		sb->s_free_blocks_count--;
		flip_map(bmap, file_block);
		if (i != num_blocks - 1) {
			memcpy((disk + EXT2_BLOCK_SIZE * file_block), file_data + copied, EXT2_BLOCK_SIZE);
			copied += EXT2_BLOCK_SIZE;
		} else {      //last block
			memset((disk + EXT2_BLOCK_SIZE * file_block), 0, EXT2_BLOCK_SIZE);
			memcpy((disk + EXT2_BLOCK_SIZE * file_block), file_data + copied, size - copied);
		}
		fnode->i_blocks += 2;
	}

	struct ext2_inode *inode = (struct ext2_inode *) (itable + (folder_inode - 1) * sizeof(struct ext2_inode));
	attach_child_to_parent(disk, inode, free_inode, EXT2_FT_REG_FILE, fname, bmap);
}

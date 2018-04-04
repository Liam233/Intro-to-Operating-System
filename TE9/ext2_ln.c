#include "ext2_functions.h"

unsigned char *disk;

int main(int argc, char **argv) {
	if (argc != 4 && argc != 5) {
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name> (-s) <absolute path> <absolute path>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;

	int flag_enabled = argc - 4;

	int inode1 = check_file_path(disk, argv[2 + flag_enabled], itable);

	char *src = format_name(argv[3 + flag_enabled]);
	char *file = get_child_name(argv[2 + flag_enabled]);
	if (get_file_inode(disk, src, file, itable) > 0) {
		perror("Link already exist");
		exit(EEXIST);
	}
	int inode2 = get_dir_inode(disk, src, itable);
	struct ext2_inode *src_inode = (struct ext2_inode *) (itable + (inode2 - 1) * sizeof(struct ext2_inode));

	char *file1 = get_child_name(argv[2 + flag_enabled]);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
	if (!flag_enabled) {
		attach_child_to_parent(disk, src_inode, inode1, EXT2_FT_REG_FILE, file1, bmap);
	}

	return 0;
}

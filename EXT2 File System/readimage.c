#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

int main(int argc, char **argv) {

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);

	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + 1024);
	printf("Inodes: %d\n", sb->s_inodes_count);
	printf("Blocks: %d\n", sb->s_blocks_count);

	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + 1024 * 2);
	printf("Block group:\n");
	printf("    block bitmap: %d\n", gd->bg_block_bitmap);
	printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
	printf("    inode table: %d\n", gd->bg_inode_table);
	printf("    free blocks: %d\n", gd->bg_free_blocks_count);
	printf("    free inodes: %d\n", gd->bg_free_inodes_count);
	printf("    used_dirs: %d\n", gd->bg_used_dirs_count);

	printf("Block bitmap: ");
	unsigned char *blockmap = (unsigned char *) (disk + 1024 * gd->bg_block_bitmap);
	unsigned char *copy = blockmap;
	int byte, bit;
	for (byte = 0; byte < (sb->s_blocks_count / 8); byte++) {
		for (bit = 0; bit < 8; bit++) {
			printf("%d", (*copy >> bit) & 1);
		}
		copy++;
		printf(" ");
	}
	printf("\n");

	printf("Inode bitmap: ");
	unsigned char *imap = (unsigned char *) (disk + 1024 * gd->bg_inode_bitmap);
	copy = imap;

	for (byte = 0; byte < (sb->s_blocks_count / 32); byte++) {
		for (bit = 0; bit < 8; bit++) {
			printf("%d", (*copy >> bit) & 1);
		}
		copy++;
		printf(" ");
	}
	printf("\n");
	printf("\n");

	printf("Inodes:\n");
	int nodes;
	for (nodes = 1; nodes < sb->s_inodes_count; nodes++) {
		struct ext2_inode *ins = (struct ext2_inode *) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table) +
														nodes * sizeof(struct ext2_inode));
		if (ins->i_size > 0 && ins->i_size <= EXT2_BLOCK_SIZE) {
			char t = 'f';
			if (ins->i_mode & EXT2_S_IFDIR) {
				t = 'd';
			}
			printf("[%d] type: %c size: %d links: %d blocks: %d\n", nodes + 1, t, ins->i_size, ins->i_links_count,
				   ins->i_blocks);
			printf("[%d] Blocks:  %d\n", nodes + 1, ins->i_block[0]);
		}
	}
	printf("\n");

	printf("Directory Blocks:\n");
	struct ext2_inode *inodes;
	struct ext2_dir_entry *dir_ent;
	int dirs;
	for (dirs = 1; dirs < sb->s_inodes_count; dirs++) {
		inodes = (struct ext2_inode *) (disk + (EXT2_BLOCK_SIZE * gd->bg_inode_table) +
										dirs * sizeof(struct ext2_inode));
		if (inodes->i_mode & EXT2_S_IFDIR && inodes->i_size > 0 && inodes->i_size <= EXT2_BLOCK_SIZE) {
			printf("   DIR BLOCK NUM: %d (for inode %d)\n", inodes->i_block[0], dirs + 1);
			int ref = 0;
			while (ref < EXT2_BLOCK_SIZE) {
				dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inodes->i_block[0] + ref);
				char t = 'u';
				if (dir_ent->file_type & EXT2_FT_REG_FILE) {
					t = 'f';
				} else if (dir_ent->file_type & EXT2_FT_DIR) {
					t = 'd';
				}
				printf("Inode: %d rec_len: %d name_len: %d type= %c name=%.*s\n", dir_ent->inode, dir_ent->rec_len,
					   dir_ent->name_len, t, dir_ent->name_len, dir_ent->name);
				ref += dir_ent->rec_len;
			}
		}
	}
	return 0;
}

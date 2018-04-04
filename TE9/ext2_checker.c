#include "ext2_functions.h"

unsigned char *disk;

int fix_counters(unsigned char *disk, int sb_or_gd, int inode_or_block);
int mode_checker(struct ext2_inode *ent_node, struct ext2_dir_entry *dir_ent, int file_link_dir);

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	int total_fixes = 0;

	//Fix counters according to bitmap
	total_fixes += abs(fix_counters(disk, 1, 1));       //superblock free inodes
	total_fixes += abs(fix_counters(disk, 1, 0));       //superblock free blocks
	total_fixes += abs(fix_counters(disk, 0, 1));       //block group free inodes
	total_fixes += abs(fix_counters(disk, 0, 0));       //block group free blocks

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	int offset = 0;     //Offset from first non reserved node.
	int cur_node = 2;       //Starts at 2 for root node, then jumps to 12 after first iteration.
	do {
		struct ext2_inode *inode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (cur_node - 1));
		if (inode->i_size > 0 && inode->i_mode & EXT2_S_IFDIR) {
			struct ext2_dir_entry *dir_ent;
			int i = 0;
			while (i < (inode->i_blocks / 2)) {
				int rec = 0;
				dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[i]);
				while (rec < EXT2_BLOCK_SIZE) {     //Check all entries
					struct ext2_inode *ent_node = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) *
					                                                              (dir_ent->inode - 1));
					//check mode and file type
					int ft;     //0 = file    1 = link    2 = dir
					for (ft = 0; ft < 3; ft++) {
						total_fixes += mode_checker(ent_node, dir_ent, ft);
					}

					//check inode bitmaps
					if (get_map(imap, dir_ent->inode) != 1) {
						flip_map(imap, dir_ent->inode);
						sb->s_free_inodes_count--;
						gd->bg_free_inodes_count--;
						total_fixes++;
						printf("Fixed: inode [%d] not marked as in-use\n", dir_ent->inode);
					}

					//deletion time
					if (ent_node->i_dtime) {        //Not 0
						ent_node->i_dtime = 0;
						total_fixes++;
						printf("Fixed: valid inode marked for deletion: [%d]\n", dir_ent->inode);
					}

					//check block bitmaps

					//Dir block map first
					int blocks_fixed = 0;
					if (get_map(bmap, inode->i_block[i]) != 1) {
						flip_map(bmap, inode->i_block[i]);
						sb->s_free_blocks_count--;
						gd->bg_free_blocks_count--;
						total_fixes++;
						blocks_fixed++;
					}

					//File blocks
					int f = 0;
					while (f < 12 && f < ent_node->i_blocks / 2) {
						if (get_map(bmap, ent_node->i_block[i]) != 1) {
							flip_map(bmap, ent_node->i_block[i]);
							sb->s_free_blocks_count--;
							gd->bg_free_blocks_count--;
							blocks_fixed++;
						}
						f++;
					}

					if (blocks_fixed) {
						total_fixes += blocks_fixed;
						printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", blocks_fixed,
						       cur_node);
					}

					rec += dir_ent->rec_len;
					dir_ent = (struct ext2_dir_entry *) (dir_ent->rec_len + (char *) dir_ent);
				}
				i++;
			}
		}
		offset++;
		cur_node = offset + EXT2_GOOD_OLD_FIRST_INO;
	} while (cur_node <= 32);

	if (total_fixes){
		printf("%d file system inconsistencies repaired!\n", total_fixes);
	}else{
		printf("No file system inconsistencies detected!\n");
	}
	return 0;
}

int fix_counters(unsigned char *disk, int sb_or_gd, int inode_or_block) {

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);

	unsigned char *copy;
	char *x;
	char *y;
	int bytes, size;
	unsigned int *sb_free;
	unsigned short *gd_free;

	if (sb_or_gd) {     //1 = superblock, 0 = block group
		x = "superblock";
		if (inode_or_block) {
			sb_free = &sb->s_free_inodes_count;
		} else {
			sb_free = &sb->s_free_blocks_count;
		}
	} else {
		x = "block group";
		if (inode_or_block) {
			gd_free = &gd->bg_free_inodes_count;
		} else {
			gd_free = &gd->bg_free_blocks_count;
		}
	}

	if (inode_or_block) {       //1 = inode, 0 = block
		copy = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
		y = "free inodes";
		bytes = sb->s_inodes_count / 8;
		size = sb->s_inodes_count;

	} else {
		copy = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
		y = "free blocks";
		bytes = sb->s_blocks_count / 8;
		size = sb->s_blocks_count - 1;      //Block bitmap is only blocks 1 - 127 (0-126)
	}

	int byte, bit;
	int used_blocks = 0;
	for (byte = 0; byte < bytes; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if ((*copy >> bit) & 1) {     //Block bitmap is only blocks 1 - 127 (0-126)
				if (inode_or_block || (byte * 8 + bit) < 127) {
					used_blocks++;      //See https://piazza.com/class/j6s7y7ws7vo45h?cid=565
				}
			}
		}
		copy++;
	}

	int diff;
	if (sb_or_gd) {
		diff = size - *sb_free - used_blocks;
		*sb_free += diff;
	} else {
		diff = size - *gd_free - used_blocks;
		*gd_free += diff;
	}
	if (diff != 0) {
		printf("Fixed: %s's %s counter was off by %d compared to the bitmap \n", x, y, abs(diff));
	}

	return diff;

}

int mode_checker(struct ext2_inode *ent_node, struct ext2_dir_entry *dir_ent, int file_link_dir) {
	int mode_type;
	int ft_type;

	if (!file_link_dir) {             //0 = file
		mode_type = EXT2_S_IFREG;
		ft_type = EXT2_FT_REG_FILE;
	} else if (file_link_dir == 1) {       //1 = link
		mode_type = EXT2_S_IFLNK;
		ft_type = EXT2_FT_SYMLINK;
	} else {                          //2 = dir
		mode_type = EXT2_S_IFDIR;
		ft_type = EXT2_FT_DIR;
	}

	if ((ent_node->i_mode & mode_type) == mode_type) {
		if (dir_ent->file_type != ft_type) {
			dir_ent->file_type = ft_type;
			printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", dir_ent->inode);
			return 1;
		}
	}
	return 0;       //No fixes
}
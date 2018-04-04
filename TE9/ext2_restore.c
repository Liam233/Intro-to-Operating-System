#include "ext2_functions.h"

unsigned char *disk;

int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <ext2 formatted virtual disk name> <absolute path>\n", argv[0]);
		exit(1);
	}
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * EXT2_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	if (argv[2][0] != '/') {
		perror("Not absolute path!");
		exit(-1);
	}
	if (argv[2][strlen(argv[2]) - 1] == '/') {
		perror("Not a file!");
		exit(ENOENT);
	}

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	char *pname = get_parent_name(argv[2]);
	char *fname = get_child_name(argv[2]);

	int fnode = get_file_inode(disk, pname, fname, itable);
	if (fnode > 0) {
		perror("File already exist.\n");
		exit(EEXIST);
	}

	int pnode = get_dir_inode(disk, pname, itable);    //Check folder exists
	if (pnode < 0) {
		perror("Can't find parent directory.\n");
		exit(ENOENT);
	}
	struct ext2_inode *src_inode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (pnode - 1));

	struct ext2_dir_entry *dir_ent;
	int i = 0;
	while (i < (src_inode->i_blocks / 2)) {

		dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * src_inode->i_block[i]);
		int rec = 0;
		while (rec < EXT2_BLOCK_SIZE) {     //Check all entries

			int required_space = dir_ent->name_len;
			if (required_space % 4) {       //Round up required space to 4 bytes
				required_space = required_space + 4 - required_space % 4;
			}
			required_space += sizeof(struct ext2_dir_entry);
			int available = dir_ent->rec_len - required_space;
			rec += required_space;

			if (!available) {       //if no space available for deleted file go to next entry
				dir_ent = (struct ext2_dir_entry *) ((char *) dir_ent + required_space);
				continue;
			}
			rec += available;
			struct ext2_dir_entry *del_ent = (struct ext2_dir_entry *) ((char *) dir_ent + required_space);
			while (0 < available) {     //Look through all available space

				//File or link only
				if (del_ent->file_type & EXT2_FT_SYMLINK || del_ent->file_type & EXT2_FT_REG_FILE) {
					if (del_ent->name_len == strlen(fname) && !strncmp(del_ent->name, fname, del_ent->name_len)) {

						//Check if inode repossessed
						if (get_map(imap, del_ent->inode)) {
							perror("Inode repossessed.\n");
							exit(ENOENT);
						}

						//Check blocks repossessed
						struct ext2_inode *del_node = (struct ext2_inode *) (itable + (del_ent->inode - 1) *
						                                                              sizeof(struct ext2_inode));
						int block = 0;
						while (block < (del_node->i_blocks / 2) && block < 12) {
							if (get_map(bmap, del_node->i_block[block])) {
								perror("Block repossessed.\n");
								exit(ENOENT);
							}
							block++;
						}

						//Reclaim blocks
						int i;
						for (i = 0; i < (del_node->i_blocks / 2); i++) {
							flip_map(bmap, del_node->i_block[i]);
							sb->s_free_blocks_count--;
							gd->bg_free_blocks_count--;
						}

						//Reclaim inode
						flip_map(imap, del_ent->inode);
						sb->s_free_inodes_count--;
						gd->bg_free_inodes_count--;

						del_ent->rec_len = available;
						dir_ent->rec_len -= available;
						del_node->i_dtime = 0;
						return 0;
					}
				}
				int delen = del_ent->name_len;
				if (delen % 4) {
					delen = delen + 4 - delen % 4;
				}
				delen += sizeof(struct ext2_dir_entry);
				del_ent = (struct ext2_dir_entry *) ((char *) del_ent + delen);
				available -= delen;
			}
		}
		i++;
	}
	perror("File couldn't be found.\n");
	exit(ENOENT);
	return 0;
}

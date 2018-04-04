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
	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);
	void *itable = disk + EXT2_BLOCK_SIZE * gd->bg_inode_table;
	unsigned char *imap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
	unsigned char *bmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);

	char *pname = get_parent_name(argv[2]);
	char *fname = get_child_name(argv[2]);
	struct ext2_inode *fnode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (check_file_path(disk,argv[2],itable) - 1));
	struct ext2_inode *pnode = (struct ext2_inode *) (itable + sizeof(struct ext2_inode) * (get_dir_inode(disk, pname, itable) - 1));

	struct ext2_dir_entry *dir_ent;
	struct ext2_dir_entry *pad_ent;
	int rec = 0;
	while (rec < EXT2_BLOCK_SIZE) {
		//pad_ent will bypass dir_ent
		pad_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * pnode->i_block[0] + rec);
		rec += pad_ent->rec_len;
		dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * pnode->i_block[0] + rec);

		if (dir_ent->file_type & EXT2_FT_REG_FILE) {
			//Find file with same name
			if (dir_ent->name_len == strlen(fname) && !strncmp(dir_ent->name, fname, dir_ent->name_len)) {
				//Set deletion time and free inode
				pad_ent->rec_len += dir_ent->rec_len;
				fnode->i_dtime = (unsigned) time(NULL);
				flip_map(imap,dir_ent->inode);
				sb->s_free_inodes_count++;
				gd->bg_free_inodes_count++;

				//free blocks
				int i;
				for (i = 0; i < (fnode->i_blocks/2); i ++){
					flip_map(bmap, fnode->i_block[i]);
					sb->s_free_blocks_count++;
					gd->bg_free_blocks_count++;
				}
				break;
			}
		}
	}
	return 0;
}

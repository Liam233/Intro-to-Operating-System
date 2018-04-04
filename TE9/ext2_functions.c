#include "ext2_functions.h"

int get_free_inode(unsigned char *imap) {
	unsigned char *copy = imap;
	int byte, bit;
	for (byte = 0; byte < 4; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if (!((*copy >> bit) & 1)) {
				return byte * 8 + bit + 1;      //Starts at 1
			}
		}
		copy++;
	}
	//Only get here if nodemap is full
	perror("No inode available.\n");
	exit(ENOENT);
}

int get_free_block(unsigned char *bmap) {
	unsigned char *copy = bmap;
	int byte, bit;
	for (byte = 0; byte < 16; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if (!((*copy >> bit) & 1)) {
				return byte * 8 + bit + 1;      //Starts at 1
			}
		}
		copy++;
	}
	//Only get here if nodemap is full
	perror("No block available.\n");
	exit(ENOENT);
}

void flip_map(unsigned char *map, int index) {
	unsigned char *copy = map;
	int byte, bit;
	for (byte = 0; byte < 16; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if (byte * 8 + bit + 1 == index) {    //Starts at 1
				*copy = *copy ^ (1 << bit);
			}
		}
		copy++;
	}
}

int get_map(unsigned char *map, int index) {        //Get map value at index
	unsigned char *copy = map;
	int byte, bit;
	for (byte = 0; byte < 16; byte++) {
		for (bit = 0; bit < 8; bit++) {
			if (byte * 8 + bit + 1 == index) {    //Starts at 1
				return (*copy >> bit) & 1;
			}
		}
		copy++;
	}
	return -1;
}

int check_file_path(unsigned char *disk, char *av, void *itable) {
	if (av[0] != '/') {
		perror("Not absolute path!");
		exit(-1);
	}
	if (av[strlen(av) - 1] == '/') {
		perror("Not a file!");
		exit(ENOENT);
	}
	char *src = get_parent_name(av);
	char *file = get_child_name(av);
	int inode = get_file_inode(disk, src, file, itable);
	if (get_file_inode(disk, src, file, itable) < 1) {
		perror("File does not exist.\n");
		exit(ENOENT);
	}
	return inode;
}

char *format_name(char *argvtwo) {
	if (argvtwo[0] != '/') {
		perror("Not absolute path!");
		exit(-1);
	}
	int plen = strlen(argvtwo);
	char *dir_path = (char *) malloc((plen + 1) * sizeof(char));    //Make input copy just in case
	strncpy(dir_path, argvtwo, plen);
	dir_path[plen] = '\0';

	while (dir_path[plen - 1] == '/' && plen > 1) {
		dir_path[plen - 1] = '\0';
		plen--;
	}

	return dir_path;
}

int get_slash_index(char *name) {
	int slash = strlen(name) - 1;
	while (name[slash] != '/' && slash > 0) {       //Get index where new dir name starts.
		slash--;
	}
	if (slash == 0 && name[slash] != '/') {       //No slash
		slash = -1;
	}
	return slash;
}

char *get_parent_name(char *name) {
	int slash = get_slash_index(name);
	if (slash == 0) {
		return "/";
	}
	char *parent_name = (char *) malloc((slash + 1) * sizeof(char));
	strncpy(parent_name, name, slash);
	parent_name[slash] = '\0';
	return parent_name;
}

char *get_child_name(char *name) {
	int slash = get_slash_index(name);
	if (slash == -1) {
		return name;
	}
	int child_len = strlen(name) - slash;
	char *child_name = (char *) malloc((child_len + 1) * sizeof(char));
	strncpy(child_name, name + slash + 1, child_len);
	child_name[child_len] = '\0';       //Child folder name (no slashes)
	return child_name;
}


int is_root(char *name) {
	if (strlen(name) == 1 && name[0] == '/') {
		return 1;
	}
	return 0;
}

int get_file_inode(unsigned char *disk, char *src_path, char *fname, void *itable) {
	int folder_inode = get_dir_inode(disk, src_path, itable);
	if (folder_inode < 1) {
		perror("Can't find source directory.\n");
		exit(ENOENT);
	}

	struct ext2_inode *inode = (struct ext2_inode *) (itable + (folder_inode - 1) * sizeof(struct ext2_inode));
	struct ext2_dir_entry *dir_ent;
	int rec = 0;
	while (rec < EXT2_BLOCK_SIZE) {
		dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + rec);
		if (dir_ent->file_type & EXT2_FT_REG_FILE) {
			if (dir_ent->name_len == strlen(fname) && !strncmp(dir_ent->name, fname, dir_ent->name_len)) {
				return dir_ent->inode;
			}
		}
		rec += dir_ent->rec_len;
	}
	return -1;  //File not found
}

int get_dir_inode(unsigned char *disk, char *name, void *itable) {
	if (is_root(name)) {
		return EXT2_ROOT_INO;
	}
	int next_dir;
	char *current_dir;
	int cur_inode = EXT2_ROOT_INO;
	int result_inode = -1;
	struct ext2_inode *inodes;

	while (strlen(name) > 1) {
		name++;    //Get rid of slash before dir name
		next_dir = 1;
		while (name[next_dir] != '/' && next_dir < strlen(name)) {       //Get index where next dir name starts.
			next_dir++;
		}

		current_dir = (char *) malloc((next_dir + 1) * sizeof(char));     //Get current dir (parent)
		strncpy(current_dir, name, next_dir);
		current_dir[next_dir] = '\0';
		name += next_dir;

		inodes = (struct ext2_inode *) (itable + (cur_inode - 1) * sizeof(struct ext2_inode));
		if (inodes->i_size == 0) {
			return -1;
		}

		int located = 0;
		if (inodes->i_mode & EXT2_S_IFDIR && inodes->i_size <= EXT2_BLOCK_SIZE) {
			int rec;
			struct ext2_dir_entry *dir_ent;
			char *dirent_name;
			int i = 0;
			while (i < (inodes->i_blocks / 2) && !located) {
				rec = 0;
				dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inodes->i_block[i]);
				while (rec < EXT2_BLOCK_SIZE) {
					rec += dir_ent->rec_len;
					if (dir_ent->file_type & EXT2_FT_DIR) {
						dirent_name = (char *) malloc((dir_ent->name_len + 1) * sizeof(char));
						strncpy(dirent_name, dir_ent->name, dir_ent->name_len);
						dirent_name[dir_ent->name_len] = '\0';
						if (!strcmp(dirent_name, current_dir)) {    //Same directory
							cur_inode = dir_ent->inode;
							result_inode = cur_inode;
							located = 1;
							break;
						}
					}
					dir_ent = (struct ext2_dir_entry *) ((char *) dir_ent + dir_ent->rec_len);
					result_inode = -1;
				}
				i++;
			}
		}
		if (result_inode == -1) {       //Not found
			return result_inode;
		}
	}
	free(current_dir);
	return result_inode;
}

int attach_child_to_parent(unsigned char *disk, struct ext2_inode *inode, int child_inode, unsigned char type,
                           char *child_name, unsigned char *bmap) {
	int rec;
	int i = 0;
	while (i < (inode->i_blocks / 2)) {
		struct ext2_dir_entry *new_dir_ent = (struct ext2_dir_entry *) (disk +
		                                                                EXT2_BLOCK_SIZE * inode->i_block[i]);
		rec = 0;
		while (rec < EXT2_BLOCK_SIZE) {
			rec += new_dir_ent->rec_len;

			int required_space = new_dir_ent->name_len;
			if (required_space % 4) {
				required_space = required_space + 4 - required_space % 4;
			}
			required_space += sizeof(struct ext2_dir_entry);
			int available = new_dir_ent->rec_len - required_space;

			if (strlen(child_name) > available) {
				new_dir_ent = (struct ext2_dir_entry *) (new_dir_ent->rec_len + (char *) new_dir_ent);
				continue;
			} else {
				new_dir_ent->rec_len = required_space;
				new_dir_ent = (struct ext2_dir_entry *) (new_dir_ent->rec_len + (char *) new_dir_ent);
				new_dir_ent->file_type = type;
				new_dir_ent->name_len = strlen(child_name);
				strncpy(new_dir_ent->name, child_name, new_dir_ent->name_len);
				new_dir_ent->rec_len = available;
				new_dir_ent->inode = child_inode;
				return 0;
			}
		}
		i++;
	}
	//No space for child_inode
	int fb = get_free_block(bmap);

	struct ext2_dir_entry *full_dir_ent = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * fb);
	full_dir_ent->inode = child_inode;
	full_dir_ent->file_type = type;
	full_dir_ent->rec_len = EXT2_BLOCK_SIZE;
	full_dir_ent->name_len = strlen(child_name);
	strncpy(full_dir_ent->name, child_name, full_dir_ent->name_len);

	inode->i_block[inode->i_blocks / 2 - 1] = fb;
	inode->i_size += EXT2_BLOCK_SIZE;
	inode->i_blocks += 2;
	flip_map(bmap, fb);

	return 0;
}


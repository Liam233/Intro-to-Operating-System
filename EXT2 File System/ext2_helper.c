#include "ext2_helper.h"

int get_dir_inode(unsigned char *disk, char *name, void *itable) {
	if (is_root(name)) {
		return EXT2_ROOT_INO;
	}
	int next_dir;
	char *current_dir;
	int cur_inode = EXT2_ROOT_INO;
	struct ext2_dir_entry *result_inode;
	struct ext2_inode *inodes;

	while (strlen(name) > 1) {
		name++;
		next_dir = 1;
		while (name[next_dir] != '/' && next_dir < strlen(name)) {
			next_dir++;
		}

		current_dir = (char *) malloc((next_dir + 1) * sizeof(char));
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
						if (!strcmp(dirent_name, current_dir)) {
							cur_inode = dir_ent->inode;
							result_inode = cur_inode;
							located = 1;
							break;
						}
					}
					dir_ent = (struct ext2_dir_entry *) ((char *) dir_ent + dir_ent->rec_len);
					result_inode = dir_ent;
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


int is_root(char *name) {
	if (strlen(name) == 1 && name[0] == '/') {
		return 1;
	}
	return 0;
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

int check_file_path(unsigned char *disk, char *absolute_path, void *itable){
	if(absolute_path[strlen(absolute_path) - 1] == '/'){
		perror("Not a file");
		exit(ENOENT);
	}
	if(absolute_path[0] != '/'){
		perror("Not an absolute path");
		exit(-1);
	}
	char *src_path = get_parent_name(absolute_path);
	char *file_name = get_child_name(absolute_path);
	int inode = get_file_inode(disk, src_path, file_name, itable);
	if(get_file_inode(disk, src_path, file_name, itable) < 1){
		perror("File does not exist.\n");
		exit(ENOENT);
	}
	return inode;
}


int get_slash_position(char *name){
	int slash_position = strlen(name) - 1;
	// Get the position of the name for the directory.
	while(slash_position > 0 && name[slash_position] != '/'){
		slash_position --;
	}
	// No slash inside.
	if(name[slash_position] != '/' && slash_position == 0){
		slash_position = -1;
	}
	return slash_position;
}


char *get_parent_name(char *name){
	int slash_position = get_slash_position(name);
	if(slash_position == 0){
		return "/";
	}
	// Allocate on heap.slash_
	char *parent_name = (char *) malloc((slash_position + 1) * sizeof(char));
	strncpy(parent_name, name, slash_position);
	parent_name[slash_position] = '\0';
	return parent_name;
}


char *get_child_name(char *name){
	int slash_position = get_slash_position(name);
	if(slash_position == -1){
		return name;
	}
	int child_name_length = strlen(name) - slash_position;
	// Allocate on heap.
	char *child_name = (char *) malloc((child_name_length + 1) * sizeof(char));
	strncpy(child_name, name + slash_position + 1, child_name_length);
	// No slashes for child folder name.
	child_name[child_name_length] = '\0';
	return child_name;
}


int check_root(char *name){
	if(strlen(name) == 1 && name[0] == '/') {
		return 1;
	}
	return 0;
}


int get_directory_inode(unsigned char *virtual_disk, char *name, void *itable){
	if(check_root(name)){
		return EXT2_ROOT_INO;
	}

	char *current_directory;
	struct ext2_inode *inodes;
	int next_directory;
	int current_inode = EXT2_ROOT_INO;
	int returned_inode = -1;

	while (strlen(name) > 1){
		// No slash.
		name = name + 1;
		next_directory = 1;

		// Get position of next directory name.
		while(next_directory < strlen(name) && name[next_directory] != '/'){
			next_directory = next_directory + 1;
		}

		// Allocate parent current directory on heap.
		current_directory = (char *) malloc((next_directory + 1) * sizeof(char));
		strncpy(current_directory, name, next_directory);
		current_directory[next_directory] = '\0';
		name = name + next_directory;

		inodes = (struct ext2_inode *) (itable + (current_inode - 1) * sizeof(struct ext2_inode));
		if(inodes->i_size == 0){
			return -1;
		}

		int flag = 0;
		if(inodes->i_size <= EXT2_BLOCK_SIZE && inodes->i_mode & EXT2_S_IFDIR){
			int count;
			struct ext2_dir_entry *directory_entry;
			char *dirent_name;
			int i = 0;
			while(!flag && i < (inodes->i_blocks / 2)){
				count = 0;
				directory_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * inodes->i_block[i]);
				while(count < EXT2_BLOCK_SIZE){
					count = count + directory_entry->rec_len;
					if(EXT2_FT_DIR & directory_entry->file_type){
						dirent_name = (char *) malloc((directory_entry->name_len + 1) * sizeof(char));
						strncpy(dirent_name, directory_entry->name, directory_entry->name_len);
						dirent_name[directory_entry->name_len] = '\0';
						// Same directory
						if(!strcmp(dirent_name, current_directory)){
							current_inode = directory_entry->inode;
							returned_inode = current_inode;
							flag = 1;
							break;
						}
					}
					directory_entry = (struct ext2_dir_entry *) ((char *) directory_entry + directory_entry->rec_len);
					returned_inode = -1;
				}
				i = i + 1;
			}
		}
		// Inode not found.
		if(returned_inode == -1){
			return returned_inode;
		}
	}
	free(current_directory);
	return returned_inode;
}


int get_file_inode(unsigned char *virtual_disk, char *src_path, char *file_name, void *itable){
	int folder_inode = get_dir_inode(virtual_disk, src_path, itable);
	if(folder_inode < 1){
		perror("Cannot find the source directory");
		exit(ENOENT);
	}

	struct ext2_inode *inode = (struct ext2_inode *) (itable + (folder_inode - 1) * sizeof(struct ext2_inode));
	struct ext2_dir_entry *directory_entry;
	int count = 0;
	while(count < EXT2_BLOCK_SIZE){
		directory_entry = (struct ext2_dir_entry *) (virtual_disk + EXT2_BLOCK_SIZE * inode->i_block[0] + count);
		if(EXT2_FT_REG_FILE & directory_entry->file_type){
			if(!strncmp(directory_entry->name, file_name, directory_entry->name_len) && directory_entry->name_len == strlen(file_name)){
				return directory_entry->inode;
			}
		}
		count = count + directory_entry->rec_len;
	}
	// Else file is not found.
	return -1;
}


int fix_counters(unsigned char *disk, int sb_or_gd, int block_or_inode){

	struct ext2_super_block *sb = (struct ext2_super_block *) (disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc *gd = (struct ext2_group_desc *) (disk + EXT2_BLOCK_SIZE * 2);

	unsigned char *save;
	char *i;
	char *j;
	int bytes;
	int size;
	unsigned int *free_sb;
	unsigned short *free_gd;

	// sb_or_gd = 1 -> super block.
	// sb_or_gd = 0 -> block group.
	if(sb_or_gd){
		i = "superblock";
		if(block_or_inode){
			free_sb = &sb->s_free_inodes_count;
		}else{
			free_sb = &sb->s_free_blocks_count;
		}
	}else{
		i = "block group";
		if(block_or_inode){
			free_gd = &gd->bg_free_inodes_count;
		}else{
			free_gd = &gd->bg_free_blocks_count;
		}
	}

	// block_or_inode = 1 -> inode.
	// block_or_inode = 0 -> block.
	if(block_or_inode){
		save = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
		j = "free inodes";
		bytes = sb->s_inodes_count / 8;
		size = sb->s_inodes_count;
	}else{
		save = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
		j = "free blocks";
		bytes = sb->s_blocks_count / 8;
		// Block bitmap is only blocks 1 - 127 (0-126).
		size = sb->s_blocks_count - 1;
	}

	int bytes_2;
	int bit;
	int used_blocks = 0;
	for(bytes_2 = 0; bytes_2 < bytes; bytes_2 ++){
		for(bit = 0; bit < 8; bit ++){
			// Block bitmap is only blocks 1 - 127 (0-126).
			if(1 & (*save >> bit)){
				if((bytes_2 * 8 + bit) < 127 || block_or_inode){
					used_blocks = used_blocks + 1;
				}
			}
		}
		save = save + 1;
	}

	int difference;
	if(sb_or_gd){
		difference = size - *free_sb - used_blocks;
		*free_sb = *free_sb + difference;
	}else{
		difference = size - *free_gd - used_blocks;
		*free_gd = *free_gd + difference;
	}
	return difference;
}


int check_mode(struct ext2_inode *entry_node, struct ext2_dir_entry *directory_entry, int file_dir_link){

	int mode;
	int file_type;

	// file_dir_link = 0 -> file
	// file_dir_link = 1 -> link
	// file_dir_link = 2 -> directory
	if(!file_dir_link){
		mode = EXT2_S_IFREG;
		file_type = EXT2_FT_REG_FILE;
	}else if(file_dir_link == 1){
		mode = EXT2_S_IFLNK;
		file_type = EXT2_FT_SYMLINK;
	}else{
		mode = EXT2_S_IFDIR;
		file_type = EXT2_FT_DIR;
	}

	if((mode & entry_node->i_mode) == mode){
		if(directory_entry->file_type != file_type){
			directory_entry->file_type = file_type;
			return 1;
		}
	}
	// No fixes.
	return 0;
}


void flip_map(unsigned char *map, int index){
	unsigned char *copy = map;
	int byte;
	int bit;
	for(byte = 0; byte < 16; byte ++){
		for(bit = 0; bit < 8; bit ++) {
			// It starts at 1.
			if(byte * 8 + bit + 1 == index){
				*copy = *copy ^ (1 << bit);
			}
		}
		copy = copy + 1;
	}
}


// Get the map value at index.
int get_map(unsigned char *map, int index){
	unsigned char *copy = map;
	int byte;
	int bit;
	for(byte = 0; byte < 16; byte ++){
		for(bit = 0; bit < 8; bit ++){
			// It starts at 1.
			if(byte * 8 + bit + 1 == index){
				return 1 & (*copy >> bit);
			}
		}
		copy = copy + 1;
	}
	return -1;
}

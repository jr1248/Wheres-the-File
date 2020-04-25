#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <openssl/sha.h>

char dashes[65] = "----------------------------------------------------------------";

/* Used by add and remove; will return locaton of matching file's hash or version number depending on flag */
unsigned int tokenize(char *path, char *input, char *hash, int flag, int *version) {
	if (input == NULL) {
		return 0;
	}
	char whole_string[strlen(input)];
	strcpy(whole_string, input);
	char *token;
	unsigned int byte_count = 0;
	unsigned int prev_bytes = 0;
	unsigned int check_bytes = 0;
	short check = 0;
	/* First token will always be the project version number, useless for now */
	token = strtok(whole_string, "\n\t");
	prev_bytes = strlen(token) + 1;
	byte_count += prev_bytes;
	int count = 0;

	while (token != NULL) {
		token = strtok(NULL, "\n\t");
		++count;
		if (token == NULL) {
			break;
		}
		if (count % 3 == 1 && version != NULL) {
			*version = atoi(token);
		}
		if (check == 1) {
			if (strcmp(token, hash) == 0 && !flag) {
				/* If the two hashes are equal, no need to put in new hash */
				return -1;
			} else {
				/* Will either return location of hash or location of file version number */
				return byte_count - (flag * check_bytes) - (flag * prev_bytes);
			}
		}
		if (strcmp(token, path) == 0) {
			/* If path found in .Manifest, check its hash, which will be next token */
			check = 1;
		}
		check_bytes = prev_bytes;
		prev_bytes = strlen(token) + 1;
		byte_count += prev_bytes;
	}
	return strlen(input);
}

/* Add a file to a .Manifest */
int add(int fd_mani, char *hashcode, char *path, char *input, int flag) {
	int *version = (int *) malloc(sizeof(int));
	*version = 0;
	/* Flag is 1 for operations that might increment file's version number, like push or upgrade */
	/* Else, for operations like add, will be 0 */
	int move = tokenize(path, input, hashcode, flag, version);
	if (move == 0) {
		fprintf(stderr, "ERROR: Could not read .Manifest.\n");
		return -1;
	}
	/* -1 means file's new hash matches hash already in .Manifest */
	if (move == -1 && !flag) {
		fprintf(stderr, "ERROR: File already up-to-date in .Manifest.\n");
		return -1;
	}

	lseek(fd_mani, move, SEEK_SET);
	/* If file wasn't in .Manifest, automatically will be added to the end with a starting version of 0 */
	if (move == strlen(input)) {
		write(fd_mani, "0\t", 2);
		write(fd_mani, path, strlen(path));
		write(fd_mani, "\t", 1);
		write(fd_mani, hashcode, strlen(hashcode));
		write(fd_mani, "\n", 1);
	} else if (!flag) {
		/* If it was in .Manifest and flag is not set, just update hash */
		write(fd_mani, hashcode, strlen(hashcode));
	} else {
		/* If it was and flag was set, increment file version number and update hash */
		int update = *version + 1;
		char update_string[sizeof(update) + 2];
		snprintf(update_string, sizeof(update) + 2, "%d\t", update);
		write(fd_mani, update_string, strlen(update_string));
		write(fd_mani, path, strlen(path));
		write(fd_mani, "\t", 1);
		write(fd_mani, hashcode, strlen(hashcode));
		write(fd_mani, "\n", 1);
	}
	return 0;
}

/* Remove a file from .Manifest by showing its hash code as a series of dashes */
int removeFile(int fd_mani, char *path, char *input) {
	/* Will never have to update version number of file being removed, so flag is always 0 */
	int move = tokenize(path, input, dashes, 0, NULL);
	/* Don't do anything if file isn't in .Manifest */
	if (move == strlen(input)) {
		fprintf(stderr, "ERROR: File \"%s\" not in .Manifest file.", path);
		return -1;
	}
	if (move == -1) {
		fprintf(stderr, "ERROR: File \"%s\" already removed from \".Manifest\" file.\n", path);
		return -1;
	}
	/* Write dashes in place of hash */
	lseek(fd_mani, move, SEEK_SET);
	write(fd_mani, dashes, strlen(dashes));
	return 0;
}

int exists(char* path) {
	DIR* dir = opendir(path);
	if (dir == NULL) {
		closedir(dir);
		return -1;
	}
	closedir(dir);
	return 0;
}

/* Remove a directory by recursively removing all of its files and subdirectories */
int remove_directory(char* path) {
	DIR* dir;
	size_t len = strlen(path);
	int ret = -1;
	if (!(dir = opendir(path))) {
		fprintf(stderr, "ERROR: Could not open project \"%s\" on server's side.\n", (path + 17));
		closedir(dir);
		return ret;
	}

	struct dirent* de;
	ret = 0;
	while ((de = readdir(dir)) != NULL) {
		/* Skip self and parent directory */
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
			continue;
		}
		/* Create new path name with current file's/directory's name attached to end of given path */
		char* p = (char*)malloc(strlen(de->d_name) + strlen(path) + 2);
		if (p != NULL) {
			snprintf(p, strlen(de->d_name) + strlen(path) + 2, "%s/%s", path, de->d_name);
			/* If a directory, recurse */
			if (de->d_type == DT_DIR) {
				ret = remove_directory(p);
			}
			else {
				/* Else, remove the file */
				ret = remove(p);
			}
			if (ret < 0) {
				fprintf(stderr, "ERROR: Failed to delete \"%s\" from server. It is probably currently in use.\n", p);
				closedir(dir);
				return ret;
			}
		}
		free(p);
	}
	/* Finally, remove the directory itself */
	closedir(dir);
	ret = rmdir(path);
	if (ret < 0) {
		fprintf(stderr, "ERROR: Failed to delete \"%s\" from server. It is probably currently in use.\n", path);
	}
	return ret;
}

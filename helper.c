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

int get_file_size(int fd) {
	struct stat st = {0};
	if (fstat(fd, &st) < 0) {
		fprintf(stderr, "ERROR: fstat() failed.\n");
		return -1;
	}
	return st.st_size;
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

/* Used by commit() below; return codes listed below:
 * -1 if error
 * 1 if path is present in .Manifest and hashes are different
 * 0 if path is present in .Manifest and hashes are same
 * 2 if path is not present in .Manifest */
int commit_helper(int version, char* path, char* hash, char* other_manifest) {
	char manifest_input[strlen(other_manifest)];
	strcpy(manifest_input, other_manifest);
	char* tok = strtok(manifest_input, "\t\n");
	int count = 0;
	int hash_check = 0;
	int v = 0;
	while (tok != NULL) {
		tok = strtok(NULL, "\t\n");
		++count;
		if (tok == NULL) {
			break;
		}
		if (count % 3 == 1) {
			v = atoi(tok);
		} else if (count % 3 == 2) {
			if (strcmp(path, tok) == 0) {
				hash_check = 1;
			}
		} else if (count % 3 == 0) {
			if (hash_check) {
				int check = strcmp(hash, tok);
				if (check != 0 && v >= version) {
					return -1;
				} else if (check != 0) {
					return 1;
				} else if (check == 0) {
					return 0;
				}
			}
		}
	}
	return 2;
}

/* Parse through client's .Manifest and use commit_helper() to determine if there are any differences
 * between client's .Manifest and server's */
int commit(int commit_fd, char* client_manifest, char* server_manifest) {
	int len = strlen(client_manifest);

	int i = 0, j = 0;
	char* tok;
	int tok_len = 0;
	int last_sep = 0;
	int count = -1;
	int version = 0;
	unsigned char hash[SHA256_DIGEST_LENGTH];
	char hashed[SHA256_DIGEST_LENGTH * 2 + 1];
	char* path = NULL;
	int delete_check = 0;
	for (i = 0; i < len; ++i) {
		/* Tokenize by creating a new token whenever whitespace is encountered */
		if (client_manifest[i] != '\t' && client_manifest[i] != '\n') {
			++tok_len;
			continue;
		}
		else {
			tok = (char*)malloc(tok_len + 1);
			for (j = 0; j < tok_len; ++j) {
				tok[j] = client_manifest[last_sep + j];
			}
			tok[tok_len] = '\0';
			last_sep += tok_len + 1;
			tok_len = 0;
			++count;
		}
		if (count == 0) {
			free(tok);
			continue;
		}
		if (count % 3 == 1) {
			/* New version of file */
			version = atoi(tok);
			++version;
			free(tok);
		}
		else if (count % 3 == 2) {
			/* File's path */
			int fd = open(tok, O_RDONLY);
			int size = get_file_size(fd);
			path = (char*)malloc(strlen(tok) + 1);
			strcpy(path, tok);
			path[strlen(tok)] = '\0';
			if (fd < 0 || size < 0) {
				delete_check = 1;
				close(fd);
				continue;
			}
			char buff[size + 1];
			read(fd, buff, size);
			buff[size] = '\0';
			SHA256(buff, strlen(buff), hash);
			/* Get file's hash too */
			int i = 0;
			for (i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
				sprintf(hashed + (i * 2), "%02x", hash[i]);
			}
			free(tok);
			close(fd);
		}
		else {
			int tok_dash = strcmp(tok, dashes);
			int commit_check;
			/* Sometimes will try to open a file that doesn't exist anymore; have to check if that file
			 * was removed from .Manifest */
			 if (tok_dash != 0 && delete_check) {
 				free(path);
 				return -1;
 			}
			else if (tok_dash == 0) {
				/* Run commit_helper() to see if file was removed from .Manifest already */
				delete_check = 0;
				strcpy(hashed, dashes);
				commit_check = commit_helper(version - 1, path, dashes, server_manifest);
			}
			else {
				commit_check = commit_helper(version - 1, path, hashed, server_manifest);
			}
			if (commit_check == -1) {
				return -1;
			}

			if (tok_dash == 0 && commit_check == 1) {
				/* If file is present and its hash isn't dashes, and it was removed from local .Manifest, D*/
				write(commit_fd, "D\t", 2);
			}
			else if (tok_dash != 0 && commit_check == 2) {
				/* If file wasn't removed from local .Manifest and it's not present, A */
				write(commit_fd, "A\t", 2);
			}
			else if (tok_dash != 0 && commit_check == 1) {
				/* If file wasn't removed from local .Manifest, it's present in server's .Manifest, and
				 * the hashes aren't the same, M */
				write(commit_fd, "M\t", 2);
			}
			else  {
				continue;
			}
			/* If code was given, write this into .Commit */
			char* v = (char*)malloc(sizeof(version) + 1);
			snprintf(v, sizeof(version) + 1, "%d", version);
			write(commit_fd, v, strlen(v));
			write(commit_fd, "\t", 1);
			write(commit_fd, path, strlen(path));
			write(commit_fd, "\t", 1);
			write(commit_fd, hashed, strlen(hashed));
			write(commit_fd, "\n", 1);
			free(v);
			free(tok);
		}
	}
	return 1;
}

/* Get rid of all .Commits that don't match arg name */
int delete_commits(char *proj_path, char *name) {
	DIR *dir;
	if (!(dir = opendir(proj_path))) {
		fprintf(stderr, "ERROR: Could not open \"%s\" on server.\n", proj_path);
		closedir(dir);
		return -1;
	}
	struct dirent *de;
	/* Open .server_directory/proj_path */
	while ((de = readdir(dir)) != NULL) {
		/* If .Commit is part of the name, delete te file */
		if (strstr(de->d_name, ".Commit") != NULL && strcmp(de->d_name, name) != 0) {

			char *comm_path = (char *) malloc(strlen(proj_path) + strlen(de->d_name) + 1);

			snprintf(comm_path, strlen(proj_path) + strlen(de->d_name) + 1, "%s%s", proj_path, de->d_name);
			if (de->d_type != DT_DIR) {
				remove(comm_path);
			}
			free(comm_path);
		}
	}
	closedir(dir);
	return 0;
}

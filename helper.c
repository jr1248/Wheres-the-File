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
int commit(int commit_fd, char* client_manifestfest, char* smfest) {
	int len = strlen(client_manifestfest);

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
		if (client_manifestfest[i] != '\t' && client_manifestfest[i] != '\n') {
			++tok_len;
			continue;
		}
		else {
			tok = (char*)malloc(tok_len + 1);
			for (j = 0; j < tok_len; ++j) {
				tok[j] = client_manifestfest[last_sep + j];
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
				commit_check = commit_helper(version - 1, path, dashes, smfest);
			}
			else {
				commit_check = commit_helper(version - 1, path, hashed, smfest);
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

/* Copy src directory into dest directory recursively */
int dir_copy(char *src, char *dest, int flag) {
	DIR *dir;
	struct dirent *de;
	if ((dir = opendir(src)) == NULL) {
		fprintf(stderr, "ERROR: Cannot open directory \"%s\".\n", src);
		closedir(dir);
		return -1;
	}
	while ((de = readdir(dir)) != NULL) {
		/* Skip self and parent directory */
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
			continue;
		}
		if (flag && (strstr(de->d_name, ".Manifest") != NULL || strstr(de->d_name, ".Commit") != NULL || strstr(de->d_name, ".History") != NULL)) {
			continue;
		}
		char *new_src_path = (char *) malloc(strlen(src) + strlen(de->d_name) + 2);
		char *new_dest_path = (char *) malloc(strlen(dest) + strlen(de->d_name) + 2);
		snprintf(new_src_path, strlen(src) + strlen(de->d_name) + 2, "%s/%s", src, de->d_name);
		snprintf(new_dest_path, strlen(dest) + strlen(de->d_name) + 2, "%s/%s", dest, de->d_name);
		/* If de is a directory, create a new matching one in dest and fill it recursively */
		if (de->d_type == DT_DIR) {
			mkdir(new_dest_path, 0777);
			if (dir_copy(new_src_path, new_dest_path, flag) != 0) {
				return -1;
			} else {
				return 0;
			}
		} else {
			/* Otherwise, just copy src's file's contents into newly created dest's file */
			int fd_src_file = open(new_src_path, O_RDONLY);
			if (fd_src_file < 0) {
				fprintf(stderr, "ERROR: Cannot open file \"%s\".\n", new_src_path);
				close(fd_src_file);
				closedir(dir);
				return -1;
			}
			int fd_dest_file = open(new_dest_path, O_CREAT | O_WRONLY, 0777);
			if (fd_dest_file < 0) {
				fprintf(stderr, "ERROR: Cannot create file \"%s\".\n", new_dest_path);
				close(fd_dest_file);
				close(fd_src_file);
				closedir(dir);
				return -1;
			}
			int size = get_file_size(fd_src_file);
			if (size < 0) {
				fprintf(stderr, "ERROR: Cannot get size of file \"%s\".\n", new_src_path);
				close(fd_dest_file);
				close(fd_src_file);
				closedir(dir);
				return -1;
			}
			char input[size + 1];
			read(fd_src_file, input, size);
			input[size] = '\0';
			write(fd_dest_file, input, size);
			close(fd_dest_file);
			close(fd_src_file);
		}
		free(new_src_path);
		free(new_dest_path);
	}
	closedir(dir);
	return 0;
}

int create_dirs(char *file_path, char *parent, int flag) {
	char *path_token;
	char *prev_token = (char *) malloc(strlen(parent) + 2);
	snprintf(prev_token, strlen(parent) + 2, "%s/", parent);
	char *save_token = (char *) malloc(strlen(parent) + 2);
	int j = 0, k = 0;
	int last_sep = 0;
	int token_len = 0;
	int len = strlen(file_path);
	int count = 0;
	for (j = 0; j < len; ++j) {
		if (file_path[j] != '/') {
			++token_len;
			continue;
		} else {
			path_token = (char *) malloc(token_len + 1);
			for (k = 0; k < token_len; ++k) {
				path_token[k] = file_path[last_sep + k];
			}
			path_token[token_len] = '\0';
			last_sep += token_len + 1;
			token_len = 0;
			++count;
			if (count <= 1 + flag) {
				free(path_token);
				continue;
			}
		}
		free(save_token);
		save_token = (char *) malloc(strlen(prev_token) + 1);
		strcpy(save_token, prev_token);
		save_token[strlen(prev_token)] = '\0';
		free(prev_token);
		prev_token = (char *) malloc(strlen(save_token) + strlen(path_token) + 2);
		snprintf(prev_token, strlen(save_token) + strlen(path_token) + 2, "%s%s/", save_token, path_token);
		if (exists(prev_token) == -1) {
			mkdir(prev_token, 0777);
		}
		free(path_token);
	}
	return 0;
}

/* Check if .Commit given from client matches any on server */
int push_check(char *proj, char *commit_input) {
	char p[20 + strlen(proj)];
	snprintf(p, strlen(proj) + 20, ".server_directory/%s/", proj);
	DIR *dir;
	if (!(dir = opendir(p))) {
		fprintf(stderr, "ERROR: Could not open project \"%s\" on server.\n", proj);
		closedir(dir);
		return -1;
	}
	struct dirent *de;
	while ((de = readdir(dir)) != NULL) {
		/* Check if file's name contains .Commit */
		if (strstr(de->d_name, ".Commit") != NULL) {
			char *commit_path = (char *) malloc(strlen(p) + strlen(de->d_name) + 1);
			snprintf(commit_path, strlen(p) + strlen(de->d_name) + 1, "%s%s", p, de->d_name);
			int commit_fd = open(commit_path, O_RDONLY);
			if (commit_fd < 0) {
				free(commit_path);
				continue;
			}
			int size = get_file_size(commit_fd);
			if (size <= 0) {
				free(commit_path);
				continue;
			}
			/* Get input of file */
			char input[size + 1];
			int bytes_read = read(commit_fd, input, size);
			input[size] = '\0';
			input[size - 1] = '\0';
			/* Check if file's input matches given .Commit's */
			if (strcmp(input, commit_input) == 0) {
				free(commit_path);
				close(commit_fd);
				char name[strlen(de->d_name) + 1];
				strcpy(name, de->d_name);
				name[strlen(de->d_name)] = '\0';
				closedir(dir);
				/* Delete all other .Commits */
				int ret = delete_commits(p, name);
				return ret;
			}
			close(commit_fd);
			free(commit_path);
		}
	}
	closedir(dir);
	return 1;
}

int update_helper(char* manifest, int cv, int sv, char *hash, int fv, char* fc) {
	int count = -1;
	char* manifest_token;
	int i = 0, j = 0;
	int last_sep = 0;
	int tok_len = 0;
	int len = strlen(manifest);
	int version = 0;
	char* file_path = NULL;
	int check = 0;
	for (i = 0; i < len; ++i) {
		if (manifest[i] != '\t' && manifest[i] != '\n') {
			++tok_len;
			continue;
		} else {
			manifest_token = (char *) malloc(tok_len + 1);
			for (j = 0; j < tok_len; ++j) {
					manifest_token[j] = manifest[last_sep + j];
				}
			manifest_token[tok_len] = '\0';
			last_sep += tok_len + 1;
			tok_len = 0;
			++count;
		}
		if (count == 0) {
			free(manifest_token);
			continue;
		}
		if (count % 3 == 1) {
			version = atoi(manifest_token);
			free(manifest_token);
		} else if (count % 3 == 2) {
			if (strcmp(fc, manifest_token) == 0) {
				check = 1;
			}
			free(manifest_token);
		} else if (count % 3 == 0) {
			if (check == 1) {
				if (strcmp(hash, dashes) == 0 && strcmp(manifest_token, dashes) == 0) {
					return 5;
				} else if (strcmp(hash, dashes) == 0 && strcmp(manifest_token, dashes) != 0 && cv != sv) {
					return 3;
				} else if (strcmp(hash, dashes) != 0 && strcmp(manifest_token, dashes) == 0) {
					if (cv == sv) {
						return 4;
					} else {
						return 1;
					}
				} else if (strcmp(hash, manifest_token) == 0 && fv != version && cv != sv) {
					free(manifest_token);
					return 2;
				} else if (strcmp(hash, manifest_token) != 0 && cv == sv) {
					free(manifest_token);
					return 1;
				} else if (strcmp(hash, manifest_token) != 0 && cv != sv && fv != version) {
					free(manifest_token);
					return -1;
				}
				free(manifest_token);
				break;
			}
		}
	}
	/* Repeat for final token missed by for loop */
	if (check == 1 && tok_len > 0) {
		manifest_token = (char *) malloc(tok_len + 1);
		for (i = 0; i < tok_len; ++i) {
			manifest_token[i] = manifest[last_sep + 1];
		}
		manifest_token[tok_len] = '\0';
		if (strcmp(hash, dashes) == 0 && strcmp(manifest_token, dashes) == 0) {
				return 5;
		} else if (strcmp(hash, dashes) == 0 && strcmp(manifest_token, dashes) != 0 && cv != sv) {
				return 3;
		} else if (strcmp(hash, dashes) != 0 && strcmp(manifest_token, dashes) == 0) {
			if (cv == sv) {
				return 4;
			} else {
				return 1;
			}
		} else if (strcmp(hash, manifest_token) == 0 && fv != version && cv != sv) {
			free(manifest_token);
			return 2;
		} else if (strcmp(hash, manifest_token) != 0 && cv == sv) {
			free(manifest_token);
			return 1;
		} else if (strcmp(hash, manifest_token) != 0 && cv != sv && fv != version) {
			free(manifest_token);
			return -1;
		}
	}
	if (cv == sv) {
		return 1;
	} else {
		return 4;
	}
}

/* Fill .Update with the help of update_helper() */
int update(int update_fd, char* client_manifest, char* sm, int cv, int sv) {
	/* Tokenize client's .Manifest */
	int count = -1;
	char* manifest_token = NULL;
	int i = 0, j = 0;
	int last_sep = 0;
	int tok_len = 0;
	int len = strlen(client_manifest);
	int version = 0;
	int res = 1;
	char hashed[SHA256_DIGEST_LENGTH * 2 + 1];
	char* p = NULL;
	for (i = 0; i < len; ++i) {
		if (client_manifest[i] != '\t' && client_manifest[i] != '\n') {
				++tok_len;
				continue;
		} else {
			if (manifest_token != NULL) {
				free(manifest_token);
			}
			manifest_token = (char *) malloc(tok_len + 1);
			for (j = 0; j < tok_len; ++j) {
				manifest_token[j] = client_manifest[last_sep + j];
			}
			manifest_token[tok_len] = '\0';
			last_sep += tok_len + 1;
			tok_len = 0;
			++count;
		}

		/* First token will always be the .Manifest version number, which is already given in args */
		if (count == 0) {
			continue;
		}
		if (count % 3 == 1) {
			/* File version */
			version = atoi(manifest_token);
		} else if (count % 3 == 2) {
			/* Path */
			int fd = open(manifest_token, O_RDONLY);
			int size = get_file_size(fd);
			if (fd < 0 || size < 0) {
				fprintf(stderr, "Cannot read \"%s\".", manifest_token);
				return -1;
			}
			char buff[size + 1];
			read(fd, buff, size);
			buff[size] = '\0';
			/* Get hash too */
			unsigned char hash[SHA256_DIGEST_LENGTH];
			SHA256(buff, strlen(buff), hash);
			int k = 0;
			for (k = 0; k < SHA256_DIGEST_LENGTH; ++k) {
				sprintf(hashed + (k * 2), "%02x", hash[k]);
			}
			hashed[SHA256_DIGEST_LENGTH * 2] = '\0';
			p = (char*)malloc(strlen(manifest_token) + 1);
			strcpy(p, manifest_token);
			p[strlen(manifest_token)] = '\0';
			close(fd);
		} else if (count != 0) {
			int update_check = 0;
			/* Check if file was already deleted in server */
			if (strcmp(manifest_token, dashes) == 0) {
				update_check = update_helper(sm, cv, sv, dashes, version, p);
			} else {
				/* Or just do a regular check */
				update_check = update_helper(sm, cv, sv, hashed, version, p);
			}
			/* If conflict, stop printing anything else and report conflict */
			if (update_check == -1) {
				res = 0;
				printf("CONFLICT: %s\n", p);
			}
			if (res) {
				/* Skip any U codes */
				if (update_check == 2) {
					write(update_fd, "M\t", 2);
					printf("M\t");
				} else if (update_check == 3) {
					write(update_fd, "A\t", 2);
					printf("A\t");
				} else if (update_check == 4) {
					write(update_fd, "D\t", 2);
					printf("D\t");
				}
 				if (update_check > 1 && update_check < 5) {
					char vers[sizeof(version) + 1];
					snprintf(vers, sizeof(version), "%d", version);
					vers[sizeof(version)] = '\0';
					write(update_fd, vers, strlen(vers));
					write(update_fd, "\t", 1);
					write(update_fd, p, strlen(p));
					write(update_fd, "\t", 1);
					write(update_fd, hashed, strlen(hashed));
					write(update_fd, "\n", 1);
					printf("%d\t%s\t%s\n", version, p, hashed);
				}
			}
			free(p);
		}
	}
	/* Also need to check for files that are on server that just aren't on client's .Manifest at all, even as removed */
	if (res) {
		count = -1;
		manifest_token = NULL;
		last_sep = 0;
		tok_len = 0;
		len = strlen(sm);
		version = 0;
		p = NULL;
		for (i = 0; i < len; ++i) {
			if (sm[i] != '\t' && sm[i] != '\n') {
					++tok_len;
					continue;
			} else {
				manifest_token = (char *) malloc(tok_len + 1);
				for (j = 0; j < tok_len; ++j) {
					manifest_token[j] = sm[last_sep + j];
				}
				manifest_token[tok_len] = '\0';
				last_sep += tok_len + 1;
				tok_len = 0;
				++count;
			}
			/* First token will always be the .Manifest version number, which is already given in args */
			if (count == 0) {
				free(manifest_token);
				continue;
			}
			if (count % 3 == 1) {
			/* File version */
				version = atoi(manifest_token);
			} else if (count % 3 == 2) {
			/* Path */
				p = (char *) malloc(strlen(manifest_token) + 1);
				strcpy(p, manifest_token);
				p[strlen(manifest_token)] = '\0';
				free(manifest_token);
			} else {
				int update_check = update_helper(client_manifest, cv, sv, manifest_token, version, p);
				/* Only need to check for code 4: path not found in other mani and versions different */
				if (strcmp(manifest_token, dashes) != 0 && update_check == 4) {
					write(update_fd, "A\t", 2);
					printf("A\t");
					char vers[sizeof(version) + 1];
					snprintf(vers, sizeof(version), "%d", version);
					vers[sizeof(version)] = '\0';
					write(update_fd, vers, strlen(vers));
					write(update_fd, "\t", 1);
					write(update_fd, p, strlen(p));
					write(update_fd, "\t", 1);
					write(update_fd, manifest_token, strlen(manifest_token));
					write(update_fd, "\n", 1);
					printf("%d\t%s\t%s\n", version, p, manifest_token);
				}
				free(manifest_token);
				free(p);
			}
		}
	}
	return res;
}

/* tfortune: fortune with recursive directory traversal */

#define _BSD_SOURCE  /* for non-standard DT_DIR constant */

#include <arpa/inet.h>  /* for uint32_t & htonl */
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>  /* for time(), to seed rand() */
#include <unistd.h>

#define STRFILE_HEADER_SIZE ((5 * sizeof(uint32_t)) + 1)

typedef struct Dire {
	DIR* d;
	struct dirent* en;
} Dire;

typedef struct Jar {
	char* dat;
	unsigned int num_fortunes;
	unsigned int min_len;
	unsigned int max_len;
	char delim;
	unsigned long int file_size;
} Jar;

typedef struct Jars {
	Jar* j;
	unsigned int count;
	unsigned int capacity;
	unsigned int num_fortunes;
} Jars;

unsigned char Jars_init(Jars* js, unsigned int initial_capacity)
{
	js->count = 0;
	js->capacity = initial_capacity;
	js->num_fortunes = 0;

	if (js->capacity == 0) {
		js->j = NULL;
	} else if ((js->j = malloc(js->capacity * sizeof(Jar))) == NULL) {
		fprintf(stderr, "Cannot allocate %lu bytes of memory for fortune file list.\n", js->capacity * sizeof(Jar));
		return 0;
	}
	return 1;
}

unsigned char Jars_add(Jars* js, const char* dat_file_path)
{
	char dat_header[STRFILE_HEADER_SIZE];
	FILE* fh;
	struct stat file_info;
	Jar* j;
	char* last_dot_ptr;
	Jar* old_jar_list_ptr;

	/* If the jar list is full, try to make it bigger. */
	if (js->count == js->capacity) {
		js->capacity = (js->capacity + 1) * 2;
		old_jar_list_ptr = js->j;
		if ((js->j = realloc(js->j, js->capacity * sizeof(Jar))) == NULL) {
			fprintf(stderr, "Cannot allocate %lu bytes of memory for fortune file list.\n", js->capacity * sizeof(Jar));
			js->capacity = (js->capacity / 2) - 1;
			js->j = old_jar_list_ptr;
			return 0;
		}
	}

	/* Set up a convenient pointer to the appropriate Jar slot for storing
	   this jar's metadata, then copy its dat file's path into it. */
	j = &((js->j)[js->count]);
	if ((j->dat = strdup(dat_file_path)) == NULL) {
		fprintf(stderr, "Cannot copy path to fortune data file %s.\n", dat_file_path);
		return 0;
	}

	/* Open the dat file and read its strfile header. */
	if ((fh = fopen(j->dat, "rb")) == NULL) {
		fprintf(stderr, "Cannot open fortune data file %s.\n", dat_file_path);
		free(j->dat);
		return 0;
	}
	if (fread(dat_header, STRFILE_HEADER_SIZE, 1, fh) != 1) {
		fprintf(stderr, "Strfile header of %s is the wrong size.\n", dat_file_path);
		free(j->dat);
		return 0;
	}

	/* Interpret the dat file's header and copy the appropriate metadata
	   from it. */
	j->num_fortunes = htonl(*(uint32_t*) (dat_header + sizeof(uint32_t)));
	j->max_len = htonl(*(uint32_t*) (dat_header + 2*sizeof(uint32_t)));
	j->min_len = htonl(*(uint32_t*) (dat_header + 3*sizeof(uint32_t)));
	j->delim = *(dat_header + 5*sizeof(uint32_t));
	if (htonl(*(uint32_t*) (dat_header + 4*sizeof(uint32_t))) != 0) {
		/* Handling fortune files with special flags is unimplemented;
		   fortunately I've yet to find a fortune file that uses any, but
		   apologize to the user if it comes up. */
		fprintf(stderr, "Fortune data file %s has special flags set, but "
		        "these will be ignored; sorry.\n", dat_file_path);
	}

	/* Close the dat file. */
	if (fclose(fh)) {
		fprintf(stderr, "Cannot close fortune data file %s.\n", j->dat);
	}

	/* Look up the corresponding fortune cookie file's size. */
	last_dot_ptr = strrchr(j->dat, '.');
	*last_dot_ptr = '\0';
	if (stat(j->dat, &file_info)) {
		fprintf(stderr, "Cannot find size of fortune file %s.\n", j->dat);
		free(j->dat);
		return 0;
	}
	j->file_size = file_info.st_size;
	*last_dot_ptr = '.';

	/* The jar's been successfully added to the jar list, so increment the
	   jar list's jar count and number of available fortune cookies. */
	(js->count)++;
	(js->num_fortunes) += j->num_fortunes;

	return 1;
}

unsigned char Jars_fortune(const Jars* js)
{
	float chance;
	char* cookie_buf;
	float cum_chance = 0.0;
	char* dat;
	FILE* fh;
	unsigned int jar_no;
	char *last_dot_ptr;
	size_t num_bytes;
	unsigned int num_offsets_read;
	uint32_t offsets[2];
	float prob = rand() / (float) RAND_MAX;

	if (js->count == 0) {
		fputs("Cannot pick a random fortune cookie from empty "
		      "fortune cookie file list!\n", stderr);
		return 0;
	}

	for (jar_no = 0; jar_no < js->count; jar_no++) {
		chance = js->j[jar_no].num_fortunes / (float) js->num_fortunes;
		if (cum_chance + chance >= prob) {
			break;
		}
		cum_chance += chance;
	}

	if (jar_no >= js->count) {
		/* If every available fortune cookie file was passed over, the
		   program must have calculated the probabilities wrong, which means
		   it miscounted the number of available fortune cookies. */
		fprintf(stderr, "Miscounted the number of available fortune cookies as %u.\n", js->num_fortunes);
		return 0;
	}

	dat = js->j[jar_no].dat;

	if ((fh = fopen(dat, "rb")) == NULL) {
		fprintf(stderr, " ---> \"%s\" %u\n", dat, jar_no);
		fprintf(stderr, "Cannot open fortune data file %s.\n", dat);
		return 0;
	}

	if (fseek(fh, (long) (6 + js->j[jar_no].num_fortunes * (rand() / (RAND_MAX + 1.0))) * sizeof(uint32_t), SEEK_SET)) {
		fprintf(stderr, "Cannot seek in fortune data file %s for offsets.\n", dat);
		return 0;
	}

	num_offsets_read = fread(offsets, sizeof(uint32_t), 2, fh);
	offsets[0] = htonl(offsets[0]);
	offsets[1] = htonl(offsets[1]);
	if (num_offsets_read == 0) {
		fprintf(stderr, "Cannot read offsets from fortune data file %s.\n", dat);
		return 0;
	} else if (num_offsets_read == 1) {
		/* There was only one offset left to be read in the dat file stream,
		   so that must have been the last offset in it, implying that that
		   offset refers to the last fortune cookie in the file. Set the
		   2nd offset to the fortune cookie file's size, indicating that
		   this function should read from the first offset to EOF. */
		offsets[1] = js->j[jar_no].file_size;
	}
	num_bytes = offsets[1] - offsets[0];

	if (fclose(fh)) {
		fprintf(stderr, "Cannot close fortune data file %s.\n", dat);
	}

	if ((cookie_buf = malloc(num_bytes)) == NULL) {
		fprintf(stderr, "Cannot allocate %lu bytes of memory for fortune cookie.\n", num_bytes);
		return 0;
	}

	last_dot_ptr = strrchr(dat, '.');
	*last_dot_ptr = '\0';

	if ((fh = fopen(dat, "rb")) == NULL) {
		fprintf(stderr, "Cannot open fortune cookie file %s.\n", dat);
		*last_dot_ptr = '.';
		return 0;
	}

	if (fseek(fh, offsets[0], SEEK_SET)) {
		fprintf(stderr, "Cannot seek in fortune file %s for cookie.\n", dat);
		*last_dot_ptr = '.';
		return 0;
	}

	if (fread(cookie_buf, num_bytes, 1, fh) != 1) {
		fprintf(stderr, "Cannot read cookie from %s.\n", dat);
		*last_dot_ptr = '.';
		return 0;
	}

	/* strfile doesn't calculate offsets so as to exclude the delimiter
	   character and newline, so check to see whether the read-in fortune
	   cookie has a delimiter and newline at the end of it, and if so,
	   deduct 2 from the cookie's byte count to hide them. Note the
	   assumption of Unix-style line endings. */
	if ((num_bytes >= 2) && (cookie_buf[num_bytes-2] == js->j[jar_no].delim) && (cookie_buf[num_bytes-1] == '\n')) {
		num_bytes -= 2;
	}

	fwrite(cookie_buf, num_bytes, 1, stdout);
	free(cookie_buf);

	if (fclose(fh)) {
		fprintf(stderr, "Cannot close fortune cookie file %s.\n", dat);
		*last_dot_ptr = '.';
		return 0;
	}

	*last_dot_ptr = '.';

	return 1;
}

void Jars_list(const char* dir_path, const Jars* js)
{
	float chance;
	const size_t dir_path_len = strlen(dir_path);
	unsigned int jar_no;
	char *p;
	char *path_end;

	if (!(js->num_fortunes)) {
		printf("  0.00%% %s\n", dir_path);
		return;
	} else {
		printf("100.00%% %s\n", dir_path);
	}

	for (jar_no = 0; jar_no < js->count; jar_no++) {
		/* Display the selection probability and short name of fortune
		   file `jar_no`. Since each file's name is stored as the path to
		   its .dat file, shave the last four characters from the name
		   before displaying it, to eliminate the superfluous ".dat". */
		chance = js->j[jar_no].num_fortunes / (float) js->num_fortunes;
		printf("    %5.2f%% ", 100.0 * chance);
		p = js->j[jar_no].dat + dir_path_len;
		path_end = p + 1;
		while (*path_end) {
			path_end++;
		}
		for (; p < path_end - 4; p++) {
			putchar(*p);
		}
		putchar('\n');
	}
}

void Jars_free(Jars* js)
{
	unsigned int jar_no;

	if (js->j != NULL) {
		for (jar_no = 0; jar_no < js->count; jar_no++) {
			free(js->j[jar_no].dat);
		}
		free(js->j);
		js->j = NULL;
	}
	js->count = 0;
	js->capacity = 0;
}

unsigned char ends_with_dot_dat(const char* s)
{
	unsigned int len = strlen(s);
	if (len < 4) {
		return 0;
	}
	return ((s[len-4] == '.') && (s[len-3] == 'd') && (s[len-2] == 'a') && (s[len-1] == 't'));
}

unsigned char is_dot_or_dot_dot(const char* s)
{
	if (s[0] != '.') {
		return 0;
	}
	return (s[1] == '\0') || ((s[1] == '.') && (s[2] == '\0'));
}

char walk_for_fortune_files(const char* dir_path, Jars* js)
{
	char* cur_dir;
	char* cur_file = NULL;
	size_t cur_file_alloc = 0;
	size_t cur_file_needing_alloc;
	unsigned int depth = 1;
	Dire* dirs;
	unsigned int dir_slots = 2;
	char* last_slash_ptr;

	if ((dirs = malloc(dir_slots * sizeof(Dire))) == NULL) {
		fputs("Cannot allocate directory record memory.\n", stderr);
		return 0;
	}

	if ((dirs[0].d = opendir(dir_path)) == NULL) {
		return 0;
	}

	if ((cur_dir = malloc(strlen(dir_path) + 1 + dir_slots * (NAME_MAX+1))) == NULL) {
		fputs("Cannot allocate directory name memory.\n", stderr);
		free(dirs);
		return 0;
	}
	strcat(cur_dir, dir_path);

	while (depth) {
		while ((dirs[depth-1].en = readdir(dirs[depth-1].d)) != NULL) {
			if (dirs[depth-1].en->d_type == DT_DIR) {
				if (is_dot_or_dot_dot(dirs[depth-1].en->d_name)) {
					continue;
				}
				if (depth == dir_slots) {
					dir_slots *= 2;
					if ((dirs = realloc(dirs, dir_slots * sizeof(Dire))) == NULL) {
						fputs("Cannot reallocate directory record memory.\n", stderr);
						free(dirs);
						free(cur_dir);
						if (cur_file != NULL) {
							free(cur_file);
						}
						return 0;
					}
					if ((cur_dir = realloc(cur_dir, strlen(cur_dir) + 1 + (NAME_MAX+1) * dir_slots)) == NULL) {
						fputs("Cannot reallocate directory name memory.\n", stderr);
						free(dirs);
						free(cur_dir);
						if (cur_file != NULL) {
							free(cur_file);
						}
						return 0;
					}
				}
				if (cur_dir[strlen(cur_dir) - 1] != '/') {
					strcat(cur_dir, "/");
				}
				strcat(cur_dir, dirs[depth-1].en->d_name);
				if ((dirs[depth].d = opendir(cur_dir)) == NULL) {
					fprintf(stderr, "Cannot open directory %s.\n", cur_dir);
					free(dirs);
					free(cur_dir);
					if (cur_file != NULL) {
						free(cur_file);
					}
					return 0;
				}
				depth++;
			} else {
				/* Allocation length explanation: length of the nonzero bytes
				   in `cur_dir`, plus length of the nonzero bytes in
				   `cur_file`, plus 2 bytes for the terminating zero byte and
				   the '/' separator. */
				cur_file_needing_alloc = 2 + strlen(cur_dir)
				                         + strlen(dirs[depth-1].en->d_name);
				if (cur_file_needing_alloc > cur_file_alloc) {
					if ((cur_file = realloc(cur_file, cur_file_needing_alloc)) == NULL) {
						fputs("Cannot allocate file path memory.\n", stderr);
						free(dirs);
						free(cur_dir);
						if (cur_file != NULL) {
							free(cur_file);
						}
						return 0;
					}
				}
				strcpy(cur_file, cur_dir);
				if (cur_dir[strlen(cur_dir) - 1] != '/') {
					strcat(cur_file, "/");
				}
				strcat(cur_file, dirs[depth-1].en->d_name);
				if (!ends_with_dot_dat(cur_file)) {
					continue;
				}
				if (!Jars_add(js, cur_file)) {
					fprintf(stderr, "Cannot add %s to fortune file list.\n", cur_file);
				}
			}
		}
		if (closedir(dirs[depth-1].d)) {
			fprintf(stderr, "Cannot close directory stream for %s.\n", cur_dir);
		}
		last_slash_ptr = strrchr(cur_dir, '/');
		if (last_slash_ptr != NULL) {
			*last_slash_ptr = '\0';
		}
		depth--;
	}

	free(dirs);
	free(cur_dir);
	if (cur_file != NULL) {
		free(cur_file);
	}

	return 1;
}

int main(int argc, char* argv[])
{
	const char fortune_dir_path[] = "/usr/share/games/fortunes/dt/";
	int getopt_option;
	Jars js;
	bool merely_list_fortune_files = false;

	if (!Jars_init(&js, 0)) {
		fputs("Cannot initialize list of fortune cookie files.\n", stderr);
		return EXIT_FAILURE;
	}

	while ((getopt_option = getopt(argc, argv, "f")) != -1) {
		switch (getopt_option) {
		case 'f':
			merely_list_fortune_files = true;
		break;
		default:
			fprintf(stderr, "Usage: %s [-f]\n", argv[0]);
		return EXIT_FAILURE;
		}
	}

	walk_for_fortune_files(fortune_dir_path, &js);

	if (merely_list_fortune_files) {
		Jars_list(fortune_dir_path, &js);
		Jars_free(&js);
		return EXIT_SUCCESS;
	}

	srand(time(NULL) + getpid() + getppid());

	if (!Jars_fortune(&js)) {
		fputs("Failed to pick out a fortune cookie.\n", stderr);
		return EXIT_FAILURE;
	}
	Jars_free(&js);

	return EXIT_SUCCESS;
}

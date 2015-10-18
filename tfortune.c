/* tfortune: fortune with recursive directory traversal */

#include <arpa/inet.h>  /* for uint32_t & htonl */
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>  /* for NAME_MAX */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>  /* for time(), to seed rand() */
#include <unistd.h>

#define DEFAULT_FORTUNE_FILE_DIR "/usr/share/games/fortunes/dt/"

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
	off_t file_size;
} Jar;

typedef struct Jars {
	Jar* j;
	unsigned int count;
	unsigned int capacity;
	unsigned int num_fortunes;
} Jars;

typedef struct Options {
	unsigned char c;  /* show file from which the fortune was sampled */
	unsigned char e;  /* all fortune files have equal selection chances */
	unsigned char f;  /* just list available fortune files */
	unsigned char w;  /* wait, to give the user time to read the fortune */
} Options;

unsigned char Jars_init(Jars* js, unsigned int initial_capacity)
{
	js->count = 0;
	js->capacity = initial_capacity;
	js->num_fortunes = 0;

	if (js->capacity == 0) {
		js->j = NULL;
	} else if ((js->j = malloc(js->capacity * sizeof(Jar))) == NULL) {
		fprintf(stderr, "Cannot allocate %lu bytes of memory "
		        "for fortune file list.\n", js->capacity * sizeof(Jar));
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
			fprintf(stderr, "Cannot reallocate %lu bytes of memory "
			        "for fortune file list.\n", js->capacity * sizeof(Jar));
			js->capacity = (js->capacity / 2) - 1;
			js->j = old_jar_list_ptr;
			return 0;
		}
	}

	/* Set up a convenient pointer to the appropriate Jar slot for storing
	   this jar's metadata, then copy its dat file's path into it. */
	j = &((js->j)[js->count]);
	if ((j->dat = strdup(dat_file_path)) == NULL) {
		fprintf(stderr, "Cannot copy path to fortune data file %s.\n",
		        dat_file_path);
		return 0;
	}

	/* Open the dat file and read its strfile header. */
	if ((fh = fopen(j->dat, "rb")) == NULL) {
		fprintf(stderr, "Cannot open fortune data file %s.\n", dat_file_path);
		free(j->dat);
		return 0;
	}
	if (fread(dat_header, STRFILE_HEADER_SIZE, 1, fh) != 1) {
		fprintf(stderr, "Strfile header of %s is the wrong size.\n",
		        dat_file_path);
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
	js->count++;
	js->num_fortunes += j->num_fortunes;

	return 1;
}

unsigned char Jars_fortune(const Jars* js, Options opts)
{
	size_t byte_idx;
	float chance;
	char* cookie_buf;
	float cum_chance = 0.0;
	char* dat;
	double delay_time = 0.0;
	FILE* fh;
	unsigned int jar_no;
	char* last_dot_ptr;
	size_t num_bytes;
	unsigned int num_offsets_read;
	uint32_t offsets[2];
	float prob;

	if (!(js->count)) {
		fputs("List of available fortune cookie files is empty.\n", stderr);
		return 0;
	}

	if (!(js->num_fortunes)) {
		fputs("The available fortune cookie files are all empty.\n", stderr);
		return 0;
	}

	/* Choose a fortune cookie file at random. */
	if (opts.e) {
		/* Choose uniformly randomly from the files. */
		jar_no = js->count * (rand() / (RAND_MAX + 1.0));
	} else {
		/* Choose a file with probability in proportion to the number
		   of fortune cookies it has. */
		prob = rand() / (float) RAND_MAX;
		for (jar_no = 0; jar_no < js->count; jar_no++) {
			chance = js->j[jar_no].num_fortunes / (float) js->num_fortunes;
			if (cum_chance + chance >= prob) {
				break;
			}
			cum_chance += chance;
		}
	}

	if (jar_no >= js->count) {
		/* If every available fortune cookie file was passed over, the
		   program must have calculated the probabilities wrongly, which
		   means it miscounted the number of available fortune cookies. */
		fprintf(stderr, "Miscounted the number of available fortune "
		        "cookies as %u.\n", js->num_fortunes);
		return 0;
	}

	dat = js->j[jar_no].dat;

	if ((fh = fopen(dat, "rb")) == NULL) {
		fprintf(stderr, "Cannot open fortune data file %s.\n", dat);
		return 0;
	}

	/* Jump to a uniformly randomly chosen fortune cookie in the
	   selected file. */
	byte_idx = (size_t) (js->j[jar_no].num_fortunes
	                     * (rand() / (RAND_MAX + 1.0)));
	byte_idx = (6 + byte_idx) * sizeof(uint32_t);
	if (fseek(fh, byte_idx, SEEK_SET)) {
		fprintf(stderr,
		        "Cannot seek in fortune data file %s for offsets.\n", dat);
		return 0;
	}

	num_offsets_read = fread(offsets, sizeof(uint32_t), 2, fh);
	offsets[0] = htonl(offsets[0]);
	offsets[1] = htonl(offsets[1]);
	if (num_offsets_read == 0) {
		fprintf(stderr, "Cannot read offsets from data file %s.\n", dat);
		return 0;
	} else if (num_offsets_read == 1) {
		/* There was only one offset left to be read in the dat file stream,
		   stream, so that must have been the last offset in it, implying
		   that offset refers to the last fortune cookie in the file. Set
		   the 2nd offset to the fortune cookie file's size, indicating
		   that this function should read from the first offset to EOF. */
		offsets[1] = js->j[jar_no].file_size;
	}
	num_bytes = offsets[1] - offsets[0];

	if (fclose(fh)) {
		fprintf(stderr, "Cannot close fortune data file %s.\n", dat);
	}

	if ((cookie_buf = malloc(num_bytes)) == NULL) {
		fprintf(stderr, "Cannot allocate %lu bytes of memory "
		        "for fortune cookie.\n", num_bytes);
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
	if ((num_bytes >= 2) && (cookie_buf[num_bytes-2] == js->j[jar_no].delim)
	    && (cookie_buf[num_bytes-1] == '\n')) {
		num_bytes -= 2;
	}

	if (opts.c) {
		printf("%s\n%%\n", dat);
	}

	fwrite(cookie_buf, num_bytes, 1, stdout);
	free(cookie_buf);

	if (fclose(fh)) {
		fprintf(stderr, "Cannot close fortune cookie file %s.\n", dat);
		*last_dot_ptr = '.';
		return 0;
	}

	*last_dot_ptr = '.';

	if (opts.w) {
		/* Compute a time period for which to wait for the user to read
		   the fortune cookie. Using the length of the entire cookie is a
		   bad idea (it gives too long a waiting time for ASCII art);
		   instead, use the number of lines, letters and numeric digits. */
		for (byte_idx = 0; byte_idx < num_bytes; byte_idx++) {
			if (isalpha(cookie_buf[byte_idx])) {
				delay_time += 0.06;
			} else if (isdigit(cookie_buf[byte_idx])) {
				delay_time += 0.03;
			} else if (cookie_buf[byte_idx] == '\n') {
				delay_time += 0.07;
			}
		}
		delay_time = 1 + (unsigned int) (opts.w * delay_time);
		sleep(delay_time);
	}

	return 1;
}

unsigned char paths_in_same_dir(const char* first, const char* second)
{
	char* p = strrchr(first, '/');
	char* q = strrchr(second, '/');

	/* The paths need not have slashes in them. One or both of them
	   might be the name of a file in the current directory. */
	if ((p == NULL) || (q == NULL)) {
		return (unsigned char) ((p == NULL) && (q == NULL));
	}

	/* If the paths `first` & `second` both lead to the same directory,
	   the string from `first` to `p` should now match the string from
	   `second` to `q`. So, for one thing, those two string should be the
	   same length; check that. Then check that the two strings match. */
	if ((p - first) != (q - second)) {
		return 0;
	}
	return (unsigned char) !memcmp(first, second, p - first);
}

/* Display the selection probability and short name of a Jar's file. */
void Jar_chance(const Jar* j, const Jars* js, size_t dir_part_len, unsigned char e_opt)
{
	float chance;
	char* p;
	char* path_end;

	/* First the selection probability... */
	if (e_opt) {
		chance = 1.0 / (float) js->count;
	} else {
		chance = j->num_fortunes / (float) js->num_fortunes;
	}
	printf("    %5.2f%% ", 100.0 * chance);

	/* ...then the file's name. Since `j->dat` is the path to the dat
	   file rather than the fortune file's name itself, shave the
	   last four characters off the path before displaying it, to
	   eliminate the superfluous ".dat". */
	p = j->dat + dir_part_len;
	path_end = p + 1;
	while (*path_end) {
		path_end++;
	}
	for (; p < path_end - 4; p++) {
		putchar(*p);
	}
	putchar('\n');
}

void Jars_list(const Jars* js, unsigned char e_opt)
{
	unsigned char dir_already_noted;
	unsigned int* dir_num_fortunes;
	unsigned int dir_path_idx;
	char** dir_paths;
	size_t dir_part_len;
	Jar* jar;
	unsigned int jar_no;
	char* last_slash;
	unsigned int num_dir_paths;

	if (!(js->count)) {
		/* `js` has no jars. The caller should've handled this case
		   itself -- when `js` is empty this function lacks the
		   information to list the directories searched. */
		return;
	}

	/* When it lists the available fortune files, this function has
	   to group them by directory, even though the fortune files in a
	   subdirectory might not be in a contiguous sequence in `js->j`.
	   This function therefore has to bear the burden of grouping
	   files itself.

	   It begins this task by recording in `dir_paths` each unique
	   subdirectory represented in `js->j`, and recording in
	   `dir_num_fortunes` the total number of fortune cookies in each
	   unique subdirectory. */
	if ((dir_paths = malloc(js->count * sizeof(char*))) == NULL) {
		fputs("Can't allocate memory for directory list.\n", stderr);
		return;
	}
	if ((dir_num_fortunes = malloc(js->count * sizeof(unsigned int))) == NULL) {
		fputs("Can't allocate memory for fortune cookie counts by directory.\n", stderr);
		free(dir_paths);
		return;
	}
	dir_paths[0] = js->j[0].dat;
	dir_num_fortunes[0] = js->j[0].num_fortunes;
	num_dir_paths = 1;
	for (jar_no = 1; jar_no < js->count; jar_no++) {
		jar = &(js->j[jar_no]);
		dir_already_noted = 0;
		for (dir_path_idx = 0; dir_path_idx < num_dir_paths; dir_path_idx++) {
			if (paths_in_same_dir(dir_paths[dir_path_idx], jar->dat)) {
				dir_num_fortunes[dir_path_idx] += jar->num_fortunes;
				dir_already_noted = 1;
				break;
			}
		}
		if (!dir_already_noted) {
			dir_paths[num_dir_paths] = jar->dat;
			dir_num_fortunes[num_dir_paths] = jar->num_fortunes;
			num_dir_paths++;
		}
	}

	/* Now `dir_paths` contains a pointer to a path in every unique
	   subdirectory represented in `js->j`, it's time to iterate over that
	   list of paths, extracting each subdirectory, picking out the fortune
	   files in it, computing the subdirectory's total selection
	   probability, displaying that and the subdirectory's path, then
	   listing the files in that subdir. and their probabilities. */
	for (dir_path_idx = 0; dir_path_idx < num_dir_paths; dir_path_idx++) {
		if (js->num_fortunes) {
			if (e_opt) {
				printf("%5.2f%% ", 100.0 / (float) num_dir_paths);
			} else {
				printf("%5.2f%% ", 100.0 * dir_num_fortunes[dir_path_idx]
				                   / (float) js->num_fortunes);
			}
		} else {
			printf("  0.00%% ");
		}
		last_slash = strrchr(dir_paths[dir_path_idx], '/');
		if (last_slash != NULL) {
			dir_part_len = last_slash - dir_paths[dir_path_idx] + 1;
			fwrite(dir_paths[dir_path_idx], dir_part_len, 1, stdout);
			putchar('\n');
		} else {
			dir_part_len = 0;
			puts("./");
		}
		for (jar_no = 0; jar_no < js->count; jar_no++) {
			jar = &(js->j[jar_no]);
			if (paths_in_same_dir(dir_paths[dir_path_idx], jar->dat)) {
				Jar_chance(jar, js, dir_part_len, e_opt);
			}
		}
	}

	free(dir_num_fortunes);
	free(dir_paths);
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
	return ((s[len-4] == '.') && (s[len-3] == 'd')
	        && (s[len-2] == 'a') && (s[len-1] == 't'));
}

unsigned char is_dot_or_dot_dot(const char* s)
{
	if (s[0] != '.') {
		return 0;
	}
	return (s[1] == '\0') || ((s[1] == '.') && (s[2] == '\0'));
}

unsigned char Jars_build_dat_file_path_and_add(Jars* js, const char* path)
{
	char* dat_file_path;
	size_t dat_file_path_len = 4 + strlen(path);

	if ((dat_file_path = malloc(dat_file_path_len)) == NULL) {
		fprintf(stderr, "Cannot allocate %lu bytes for data file path.\n",
		        dat_file_path_len);
		return 0;
	}

	strcpy(dat_file_path, path);
	strcat(dat_file_path, ".dat");

	if (!Jars_add(js, dat_file_path)) {
		fprintf(stderr, "Cannot add %s to data file list.\n", dat_file_path);
		free(dat_file_path);
		return 0;
	}

	free(dat_file_path);
	return 1;
}

unsigned char walk_for_fortune_files(const char* init_path, Jars* js)
{
	char* cur_dir;
	char* cur_path = NULL;
	size_t cur_path_alloc = 0;
	size_t cur_path_needing_alloc;
	unsigned int depth = 1;
	Dire* dirs;
	unsigned int dir_slots = 2;
	struct stat info_about_path;
	char* last_slash_ptr;

	/* Allocate initial memory for the directory hierarchy. */
	if ((dirs = malloc(dir_slots * sizeof(Dire))) == NULL) {
		fputs("Cannot allocate directory record memory.\n", stderr);
		return 0;
	}

	/* Try opening the initial path as a directory. */
	if ((dirs[0].d = opendir(init_path)) == NULL) {
		/* The initial path wouldn't open as a directory. It could be
		   non-existent or totally inaccessible, but it could just be a
		   user-supplied path to a single fortune file. Try treating it
		   as the latter. But first free the memory for the directory
		   hierarchy, which won't be needed as no directory traversal
		   will take place. */
		free(dirs);
		if (stat(init_path, &info_about_path)) {
			fprintf(stderr, "Cannot access information about path %s.\n",
			        init_path);
			return 0;
		}
		if (S_ISREG(info_about_path.st_mode)) {
			/* The starting path is indeed merely an ordinary file.
			   Presumably it's a specific fortune cookie file the user
			   wants to use; try adding the path to the list of
			   fortune files. */
			return Jars_build_dat_file_path_and_add(js, init_path);
		}
		return 0;
	}

	if ((cur_dir = malloc(strlen(init_path) + 1 + dir_slots * (NAME_MAX+1))) == NULL) {
		fputs("Cannot allocate directory name memory.\n", stderr);
		free(dirs);
		return 0;
	}
	strcpy(cur_dir, init_path);

	/* Recursively traverse directories, depth-first, looking for fortune
	   cookie files (and of course subdirectories). */
	while (depth) {

		/* Iterate over every filesystem entry in the current directory. */
		while ((dirs[depth-1].en = readdir(dirs[depth-1].d)) != NULL) {

			/* Skip the filesystem entries ./ and ../. */
			if (is_dot_or_dot_dot(dirs[depth-1].en->d_name)) {
				continue;
			}

			/* If necessary, (re)allocate memory for the current filesystem
			   entry's full path. Allocation length explanation: length of
			   the nonzero bytes in `cur_dir`, plus length of the nonzero
			   bytes in `cur_path`, plus 2 bytes for the terminating zero
			   byte and '/' separator. */
			cur_path_needing_alloc = 2 + strlen(cur_dir)
			                         + strlen(dirs[depth-1].en->d_name);
			if (cur_path_needing_alloc > cur_path_alloc) {
				if ((cur_path = realloc(cur_path, cur_path_needing_alloc)) == NULL) {
					fprintf(stderr,
					        "Cannot reallocate %lu bytes for file path.\n",
					        cur_path_needing_alloc);
					free(dirs);
					free(cur_dir);
					if (cur_path != NULL) {
						free(cur_path);
					}
					return 0;
				}
			}

			/* Build the full path of the current filesystem entry. */
			strcpy(cur_path, cur_dir);
			if (cur_dir[strlen(cur_dir) - 1] != '/') {
				strcat(cur_path, "/");
			}
			strcat(cur_path, dirs[depth-1].en->d_name);

			/* With the full path in a string, it's finally possible to
			   see what exactly is at the path. */
			if (stat(cur_path, &info_about_path)) {
				/* Then again, maybe it's still not possible.
				   (This can happen if the program gets trapped by a
				   cyclic path produced by a link -- eventually `stat`
				   throws the program for an `ELOOP`.) */
				fprintf(stderr, "Cannot access information about path %s.\n",
				        cur_path);
				continue;
			}

			if (S_ISDIR(info_about_path.st_mode)) {
				/* `cur_path` points to a subdirectory. Turn attention to
				   that, allocating further memory if needed. */
				if (depth == dir_slots) {
					dir_slots *= 2;
					if ((dirs = realloc(dirs, dir_slots * sizeof(Dire))) == NULL) {
						fputs("Cannot reallocate directory record memory.\n",
						      stderr);
						free(dirs);
						free(cur_dir);
						if (cur_path != NULL) {
							free(cur_path);
						}
						return 0;
					}
					if ((cur_dir = realloc(cur_dir, strlen(cur_dir) + 1 + (NAME_MAX+1) * dir_slots)) == NULL) {
						fputs("Cannot reallocate directory name memory.\n",
						      stderr);
						free(dirs);
						free(cur_dir);
						if (cur_path != NULL) {
							free(cur_path);
						}
						return 0;
					}
				}
				if (cur_dir[strlen(cur_dir) - 1] != '/') {
					strcat(cur_dir, "/");
				}
				strcat(cur_dir, dirs[depth-1].en->d_name);
				if ((dirs[depth].d = opendir(cur_dir)) == NULL) {
					/* The directory at `cur_dir` couldn't be opened.
					   Try to chop the inaccessible subdirectory off
					   the end of the current path and press on. */
					fprintf(stderr, "Cannot open directory %s.\n", cur_dir);
					last_slash_ptr = strrchr(strrchr(cur_dir, '/'), '/');
					if (last_slash_ptr != NULL) {
						*last_slash_ptr = '\0';
						break;
					} else {
						/* I don't see how the flow of execution
						   could ever arrive here, but if it
						   somehow does, fail safely. */
						free(dirs);
						free(cur_dir);
						if (cur_path != NULL) {
							free(cur_path);
						}
						return 0;
					}
				}
				depth++;
			} else {
				/* `cur_path` points to a file. The files of interest here
				   are strfile-generated .dat files, which presumably
				   correspond to fortune files. Check that the file's name
				   ends in ".dat"; if it does, add it to the list of such
				   files, otherwise skip it. */
				if (!ends_with_dot_dat(dirs[depth-1].en->d_name)) {
					continue;
				}
				if (!Jars_add(js, cur_path)) {
					fprintf(stderr, "Cannot add %s to data file list.\n",
					        cur_path);
				}
			}
		}

		/* Having exhausted this (sub)directory's contents, close it and
		   return attention to the parent directory. */
		if (closedir(dirs[depth-1].d)) {
			fprintf(stderr, "Cannot close directory stream for %s.\n",
			        cur_dir);
		}
		last_slash_ptr = strrchr(cur_dir, '/');
		if (last_slash_ptr != NULL) {
			*last_slash_ptr = '\0';
		}
		depth--;

	}

	free(dirs);
	free(cur_dir);
	if (cur_path != NULL) {
		free(cur_path);
	}

	return 1;
}

int main(int argc, char* argv[])
{
	int getopt_option;
	Jars js;
	unsigned int orig_optind;
	Options opts;

	/* Initialize the list of fortune cookie files with enough memory to
	   store metadata for 99 files. (More memory will be allocated for
	   the list later if needed.) */
	if (!Jars_init(&js, 99)) {
		fputs("Cannot initialize list of fortune cookie files.\n", stderr);
		return EXIT_FAILURE;
	}

	/* Initialize the list of command-line options with default values. */
	opts.c = 0;
	opts.e = 0;
	opts.f = 0;
	opts.w = 0;

	/* Interpret command-line flags. */
	while ((getopt_option = getopt(argc, argv, "cefw")) != -1) {
		switch (getopt_option) {
		case 'c': opts.c = 1; break;
		case 'e': opts.e = 1; break;
		case 'f': opts.f = 1; break;
		case 'w': opts.w++; break;  /* lengthen wait with each "-w" */
		default:
			fprintf(stderr, "Usage: %s [-cefw]\n", argv[0]);
		return EXIT_FAILURE;
		}
	}

	/* Process remaining arguments, which should be paths. */
	orig_optind = optind;
	if (optind < argc) {
		while (optind < argc) {
			walk_for_fortune_files(argv[optind++], &js);
		}
	} else {
		walk_for_fortune_files(DEFAULT_FORTUNE_FILE_DIR, &js);
	}

	if (opts.f) {
		/* The user wants a list of files from which fortune cookies
		   would be sampled, not a fortune cookie itself. */
		if (js.count) {
			Jars_list(&js, opts.e);
			Jars_free(&js);
		} else {
			for (optind = orig_optind; optind < argc; optind++) {
				printf("  0.00%% %s\n", argv[optind]);
			}
		}
		return EXIT_SUCCESS;
	}

	/* Seed the PRNG, display a random fortune, then free the memory
	   allocated for the list of fortune files before finishing. */
	srand(time(NULL) + getpid() + getppid());
	if (!Jars_fortune(&js, opts)) {
		fputs("Failed to pick out a fortune cookie.\n", stderr);
		return EXIT_FAILURE;
	}
	Jars_free(&js);

	return EXIT_SUCCESS;
}

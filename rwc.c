// rwc [-0d] [PATH...] - report when changed
//   -0  use NUL instead of newline for input/output separator
//   -d  detect deletions too (prefixed with "- ")

#include <sys/inotify.h>
#include <sys/stat.h>

#include <errno.h>
#include <libgen.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *argv0;
char ibuf[8192];
int ifd;

int dflag;
char input_delim = '\n';

static void *root = 0; // tree

int
order(const void *a, const void *b)
{
	return strcmp((char *)a, (char*)b);
}

void
add(char *file)
{
        struct stat st;

	char *dir = file;

	tsearch(strdup(file), &root, order);

	// assume non-existing files are regular files
	if (lstat(file, &st) < 0 || !S_ISDIR(st.st_mode))
		dir = dirname(file);

	if (inotify_add_watch(ifd, dir, IN_MOVED_TO | IN_CLOSE_WRITE | dflag)<0)
		fprintf(stderr, "%s: inotify_add_watch: %s: %s\n",
		    argv0, dir, strerror(errno));
}

int
main(int argc, char *argv[])
{
	int c, i;

	argv0 = argv[0];

        while ((c = getopt(argc, argv, "0d")) != -1)
		switch(c) {
		case '0': input_delim = 0; break;
		case 'd': dflag = IN_DELETE | IN_DELETE_SELF; break;
		default:
                        fprintf(stderr, "Usage: %s [-0d] [PATH...]\n", argv0);
                        exit(2);
                }

        ifd = inotify_init();
        if (ifd < 0) {
		fprintf(stderr, "%s: inotify_init: %s\n",
		    argv0, strerror(errno));
                exit(111);
	}

	i = optind;
	if (optind == argc)
		goto from_stdin;
	for (; i < argc; i++) {
		if (strcmp(argv[i], "-") != 0) {
			add(argv[i]);
			continue;
		}
from_stdin:
		while (1) {
			char *line = 0;
			size_t linelen = 0;
			ssize_t rd;

			errno = 0;
			rd = getdelim(&line, &linelen, input_delim, stdin);
			if (rd == -1) {
				if (errno != 0)
					return -1;
				break;
			}
			
			if (rd > 0 && line[rd-1] == input_delim)
				line[rd-1] = 0;  // strip delimiter

			add(line);
		}
	}

	while (1) {
		ssize_t len, i;
		struct inotify_event *ev;

		len = read(ifd, ibuf, sizeof ibuf);
		if (len <= 0) {
			fprintf(stderr, "%s: error reading inotify buffer: %s",
			    argv0, strerror(errno));
			exit(1);
		}
	
		for (i = 0; i < len; i += sizeof (*ev) + ev->len) {
			ev = (struct inotify_event *) (ibuf + i);

			if (ev->mask & IN_IGNORED)
				continue;

			if (tfind(ev->name, &root, order) ||
			    tfind(dirname(ev->name), &root, order)) {
				printf("%s%s%c",
				    (ev->mask & IN_DELETE ? "- " : ""),
				    ev->name,
				    input_delim);
				fflush(stdout);
			}
		}
	}

        return 0;
}

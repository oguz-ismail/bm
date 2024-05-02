#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <semaphore.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t notify;

static int number(const char *, int, int);
static int find(char **, int, const char *, int);
static void die(const char *);
static void usage(void);

static void
handler(int sig) {
	notify = 1;
}

int
main(int argc, char **argv) {
	int i, j, n, x;
	const char *p;
	int append, nosep;
	int prepend;
	int replace;
	const char *infile;
	int quiet;
	int runs, sample;
	int jobs;
	int parallel;
	struct timespec interval;
	int opt;
	char **dst, **oargv;
	int add;
	struct sigaction catch, oact;
	sigset_t usr1, omask;
	FILE *sort;
	sem_t *sem;
	time_t now;
	int id;
	int isparent;
	pid_t sibling;
	int shift;
	struct timespec start, end;
	pid_t child;
	intmax_t times[10000];
	intmax_t sum, min, max;

	append = 0;
	prepend = 0;
	replace = 0;
	nosep = 0;
	infile = NULL;
	quiet = 0;
	runs = 1000;
	sample = 100;
	parallel = 0;
	jobs = 1;
	interval.tv_sec = 0;
	interval.tv_nsec = 0;

	while ((opt = getopt(argc, argv, "+aAcCi:qn:k:Pj:z:")) != -1)
		switch (opt) {
		case 'A':
			nosep = 1;
		case 'a':
			append = 1;
			break;
		case 'c':
			prepend = 1;
			break;
		case 'C':
			replace = 1;
			break;
		case 'i':
			infile = optarg;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'n':
			runs = number(optarg, 1, RAND_MAX);
			break;
		case 'k':
			sample = number(optarg, 1,
				sizeof times/sizeof times[0]);
			break;
		case 'j':
			jobs = number(optarg, 1, INT_MAX);
			break;
		case 'P':
			parallel = 1;
			break;
		case 'z':
			x = number(optarg, 0, INT_MAX);
			interval.tv_sec = x/1000;
			interval.tv_nsec = (x%1000)*1000000;
			break;
		default:
			usage();
		}

	if (append+prepend+replace > 1)
		usage();

	argc -= optind;
	if (argc <= 0)
		usage();

	argv += optind;

	if (append || prepend || replace) {
		add = find(argv, argc, ";", prepend ? 1 : 0);
		if (add == -1)
			usage();

		if (append) {
			dst = argv-1;
			argv[add] = 0;
		}
		else if (prepend) {
			dst = argv+add;
		}
		else {
			dst = NULL;

			i = find(argv, add, "{}", 0);
			if (i != -1)
				dst = argv+i;

			oargv = argv;
			argv[add] = 0;
		}

		argc -= add+1;
		if ((append || (replace && dst)) && argc <= 0)
			usage();

		argv += add+1;
	}

	if (sample > runs)
		runs = sample;

	sigaction(SIGUSR1, NULL, &oact);
	signal(SIGUSR1, SIG_IGN);

	sigemptyset(&usr1);
	sigaddset(&usr1, SIGUSR1);
	sigprocmask(SIG_UNBLOCK, &usr1, &omask);

	catch.sa_handler = handler;
	sigemptyset(&catch.sa_mask);
	catch.sa_flags = 0;

	sort = popen("sort -k2,2n -k3,3n -k4,4nr", "w");
	if (!sort)
		die(NULL);

	setvbuf(sort, NULL, _IOLBF, 0);

	sem = sem_open("/bm", O_CREAT|O_EXCL, S_IRUSR|S_IWUSR, jobs);
	if (sem == SEM_FAILED)
		die(NULL);

	sem_unlink("/bm");

	time(&now);

	id = 1;
	isparent = 1;
	sibling = 0;

	if (nosep || replace) {
		while (argc > 1) {
			sibling = fork();
			if (sibling == -1) {
				die(NULL);
			}
			else if (sibling == 0) {
				argc--;
				argv++;
				id++;
				isparent = 0;
			}
			else {
				argc = 1;
				break;
			}
		}
	}
	else {
		for (i = 0; i < argc; i++) {
			if (!prepend && i == 0)
				continue;

			if (strcmp(argv[i], ";") != 0)
				continue;

			sibling = fork();
			if (sibling == -1) {
				die(NULL);
			}
			else if (sibling == 0) {
				i++;
				argc -= i;
				argv += i;
				id++;
				isparent = 0;
				i = -1;
			}
			else {
				argc = i;
				argv[i] = 0;
				break;
			}
		}

		if (!prepend && argc < 1)
			return 0;
	}

	if (append) {
		shift = argv-dst - (add+1);
		if (shift > argc)
			shift = argc;

		if (shift > 1)
			for (i = add+1; i > 0; i--)
				dst[i + shift-1] = dst[i];

		for (i = 0; i < shift; i++)
			dst[i] = argv[i];

		for (; i < argc; i++) {
			for (j = i+add; j >= i; j--)
				dst[j+1] = dst[j];

			dst[i] = argv[i];
		}

		argv = dst;
		argc += add;
	}
	else if (prepend) {
		for (i = 0; i <= argc; i++)
			dst[i] = argv[i];

		argv = dst-add;
		argc += add;
	}
	else if (replace) {
		if (dst)
			*dst = *argv;

		argv = oargv;
		argc = add;
	}

	for (i = 1; i < argc; i++) {
		n = strspn(argv[i], "\\");
		if (n == 0)
			continue;

		p = argv[i]+n;
		if (strcmp(p, ";") == 0 || strcmp(p, "{}") == 0)
			argv[i]++;
	}

	if (parallel)
		sem_wait(sem);
	
	sigprocmask(SIG_BLOCK, &usr1, NULL);
	sigaction(SIGUSR1, &catch, NULL);
	srand(now+id);

	for (i = 0; i < runs; i++) {
		if (!parallel)
			sem_wait(sem);

		clock_gettime(CLOCK_MONOTONIC, &start);

		child = fork();
		if (child == -1) {
			die(NULL);
		}
		else if (child == 0) {
			if (!freopen("/dev/null", "w", stdout))
				die("/dev/null");

			if (infile && !freopen(infile, "r", stdin))
				die(infile);

			if (quiet && dup2(1, 2) == -1)
				die(NULL);

			sigaction(SIGUSR1, &oact, NULL);
			sigprocmask(SIG_SETMASK, &omask, NULL);

			if (execvp(argv[0], argv) == -1)
				die(argv[0]);
		}

		waitpid(child, NULL, 0);
		clock_gettime(CLOCK_MONOTONIC, &end);

		if (!parallel)
			sem_post(sem);

		sigprocmask(SIG_UNBLOCK, &usr1, NULL);
		sigprocmask(SIG_BLOCK, &usr1, NULL);
		if (notify) {
			fprintf(stderr, "%3d: %3d%%\n", id, (i+1)*100 / runs);
			notify = 0;
		}

		nanosleep(&interval, NULL);

		j = i;
		if (j >= sample) {
			j = rand()/(RAND_MAX/i + 1);
			if (j >= sample)
				continue;
		}

		times[j] = (end.tv_sec-start.tv_sec)*1000000000 +
			end.tv_nsec-start.tv_nsec;
	}

	if (parallel)
		sem_post(sem);

	sem_close(sem);
	signal(SIGUSR1, SIG_IGN);
	sigprocmask(SIG_UNBLOCK, &usr1, NULL);

	sum = min = max = times[0];
	for (i = 1; i < sample; i++) {
		sum += times[i];

		if (times[i] < min)
			min = times[i];

		if (times[i] > max)
			max = times[i];
	}

	fprintf(sort, "%3d", id);
	fprintf(sort, " %13jd", sum/sample);
	fprintf(sort, " %13jd", min);
	fprintf(sort, " %13jd", max);
	fprintf(sort, " ");

	i = 0;
	n = argc;

	if (append) {
		n = argc-add;
	}
	else if (prepend) {
		i = add;
	}
	else if (replace) {
		if (dst) {
			i = dst-argv;
			n = i+1;
		}
		else {
			n = i;
		}
	}

	for (; i < n; i++) {
		if ((p = strchr(argv[i], '\n'))) {
			fprintf(sort, " %.*s...", (int)(p-argv[i]), argv[i]);
			break;
		}

		fprintf(sort, " %s", argv[i]);
	}

	fprintf(sort, "\n");

	if (sibling)
		waitpid(sibling, NULL, 0);

	if (isparent) {
		printf("%3s", "ID");
		printf(" %13s", "Average");
		printf(" %13s", "Minimum");
		printf(" %13s", "Maximum");
		printf("  %s\n", "Subject");
	}

	fflush(stdout);
	pclose(sort);

	if (isparent) {
		printf("%3s", "");
		printf(" %13s", "s ms us ns");
		printf(" %13s", "s ms us ns");
		printf(" %13s", "s ms us ns");
		printf("\n");
	}

	return 0;
}

static int
number(const char *s, int min, int max) {
	long x;
	char *end;

	errno = 0;
	x = strtol(s, &end, 10);

	if (errno != 0)
		die(s);

	if (x < min || x > max) {
		errno = ERANGE;
		die(s);
	}

	while (isspace(*end))
		end++;

	if (*end != '\0') {
		errno = EINVAL;
		die(s);
	}

	return x;
}

static int
find(char **a, int n, const char *s, int i) {
	for (; i < n; i++)
		if (strcmp(a[i], s) == 0)
			return i;

	return -1;
}

static void
die(const char *s) {
	perror(s);
	_exit(1);
}

static void
usage(void) {
	fputs("\
Usage: bm [-i infile] [-q] [-n runs] [-k sample] [-P] [-j jobs]\n\
          [-z sleep] command [';' command]...\n\
       bm -a [options...] args ';' command [';' command]...\n\
       bm -A [options...] args ';' utility [utility]...\n\
       bm -c [options...] command ';' args [';' args]...\n\
       bm -C [options...] template ';' arg [arg]...\n", stderr);
	_exit(1);
}

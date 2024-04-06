#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#if defined(__APPLE__)
#include <sys/xattr.h>
#endif

#if defined(__linux__)
#include <sys/xattr.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/types.h>
#include <sys/extattr.h>
#endif

#if defined(_AIX)
#include <sys/ea.h>
#endif


// sorted directory entries.
#define USE_SCANDIR


/* gopher server. */
/* inetd, tcpserver-style; read stdin, write stdout

assumed environment variables (set by tcpserver or inetd)

TCPLOCALIP
TCPREMOTEIP
TCPLOCALPORT
TCPREMOTEPORT

*/


const char *host = NULL;
unsigned port = 0;


void fail(const char *message) {
	write(STDOUT_FILENO, message, strlen(message));
	write(STDOUT_FILENO, "\r\n.\r\n", 5);
	exit(1);
}

int check_path(const char *cp) {
	// check for .. path components.
	int i;
	char c;
	int st = 0b01;

	for (i = 0; ;++i) {
		c = cp[i];
		st <<= 2;
		if (c == '/' || c == 0) {
			st |= 0b01;
		} else if (c == '.') {
			st |= 0b10;
		}

		if ((st & 0b11111111) == 0b01101001) return -1;

		if (c == 0) return 0;
	}
}


int classify_ext(const char *ext) {

	int i;
	uint32_t hash = 0;
	if (!ext || !*ext) return '9'; // binary

	for(i = 0; i < 4; ++i, ++ext) {
		unsigned c = *ext;
		if (!c) break;
		hash = hash << 8;
		hash |= (c | 0x20);
	}
	if (*ext) return -1;

#define _1(a) (a)
#define _2(a,b) ( ((a) << 8) | (b) )
#define _3(a,b,c) ( ((a) << 16) | ((b) << 8) | (c))
#define _4(a,b,c,d) ( ((a) << 24) | ((b) << 16) | ((c) << 8) | (d) )

	switch(hash) {
	case _1('c'):
	case _1('h'):

	case _3('a','s','m'):

	case _3('t','x','t'):
	case _4('t','e','x','t'):
		return '0';

	case _3('g','i','f'):
		return 'g';

	/* non-canonical */
	case _3('p','d','f'):
		return 'd';

	case _3('p','n','g'):
		return 'p';

	case _3('r','t','f'):
		return 'r';

	case _3('h','t','m'):
	case _4('h','t','m','l'):
		return 'h';

	default:
		return -1;
	}
#undef _1
#undef _2
#undef _3
#undef _4
}

const char *extname(const char *cp) {
	const char *rv = NULL;

	if (!cp) return NULL;	
	for(;;) {
		int c = *cp++;
		if (c == 0) return rv;
		if (c == '.') rv = cp;
		if (c == '/') rv = NULL;
	}
}

// https://www.freedesktop.org/wiki/CommonExtendedAttributes/

int classify_mime(const char *path) {

	char buffer[64];
	int size = -1;

	#if defined(__APPLE__)
	/* check the finder info file type for TEXT */
	size = getxattr(path, XATTR_FINDERINFO_NAME, buffer, 32, 0, 0);
	if (size >= 4 && memcmp("TEXT", buffer, 4) == 0)
		return '0';
	size = getxattr(path, "user.mime_type", buffer, sizeof(buffer), 0, 0);
	#endif

	#if defined(__linux__)
	size = getxattr(path, "user.mime_type", buffer, sizeof(buffer) - 1);
	#endif

	#if defined(__FreeBSD__) || defined(__NetBSD__)
	size = extattr_get_file(path, EXTATTR_NAMESPACE_USER, "user.mime_type", buffer, sizeof(buffer));
	if (size < 0)
		size = extattr_get_file(path, EXTATTR_NAMESPACE_USER, "mime_type", buffer, sizeof(buffer));
	#endif

	#if defined(_AIX)
	size = getea(path, "user.mime_type", buffer, sizeof(buffer) - 1);
	#endif

	if (size < 0) return -1;
	// buffer[size] = 0;
	if (size == 9 && memcmp(buffer, "text/html", 9) == 0) return 'h';
	if (size == 9 && memcmp(buffer, "image/png", 9) == 0) return 'p';
	if (size == 9 && memcmp(buffer, "image/gif", 9) == 0) return 'g';
	if (size == 15 && memcmp(buffer, "application/rtf", 12) == 0) return 't';
	if (size == 15 && memcmp(buffer, "application/pdf", 12) == 0) return 'd';

	if (size >= 5 && memcmp(buffer, "text/", 5) == 0) return '0';
	if (size >= 12 && memcmp(buffer, "application/", 12) == 0) return '9';
	return -1;
}

/*
 *
 */
int classify(struct stat *st, const char *name) {

	int rv;
	mode_t mode = st->st_mode;
	if ((mode & 0444) != 0444) return -1;

	if (S_ISDIR(mode)) return '1';
	if (!S_ISREG(mode)) return -1;

	rv = classify_mime(name);
	if (rv >= 0) return rv;

	rv = classify_ext(extname(name));
	if (rv >= 0) return rv;
	return '9'; /* default - binary */
}

void send_binary(int fd) {
	char buffer[4096];
	for(;;) {
		ssize_t n = read(fd, buffer, sizeof(buffer));
		if (n < 0) break;
		if (n == 0) break;
		write(STDOUT_FILENO, buffer, n);
	}
	close(fd);
}

void send_text(int fd) {

	FILE *f;
	char *buffer = 0;
	size_t cap = 0;
	ssize_t len; 

	f = fdopen(fd, "r");
	if (!f) return;

	for(;;) {
		len = getline(&buffer, &cap, f);
		if (len <= 0) break;
		if (buffer[len-1] == '\n') --len;
		if (buffer[0] == '.')
			fwrite(".", 1, 1, stdout);
		fwrite(buffer, len, 1, stdout);
		fwrite("\r\n", 2, 1, stdout);
	}
	fwrite(".\r\n", 3, 1, stdout);
	free(buffer);
	fclose(f);
}

void tab_split_4(char *in, char **out) {

	int i;

	for (i = 0; i < 4; ++i) out[i] = NULL;

	for (i = 0; i < 4; ++i) {
		char *tmp = strchr(in, '\t');
		out[i] = *in == '\t' ? NULL : in;
		if (!tmp) break;
		*tmp = 0;
		in = tmp+1;
	}
}

void print_path(const char *parent, const char *leaf) {
	
	int a;
	unsigned c;

	if (*leaf == '/') {
		fputs(leaf, stdout);
		return;
	}

	a = strlen(parent);

	// handle leading . and ..
	for(;;) {
		if (*leaf != '.') break;
		c = leaf[1];
		if (c == 0) {
			++leaf;
		}

		if (c == '/') {
			leaf += 2;
			continue;
		}

		if (c == '.') {
			c = leaf[2];
			if (c == 0) {
				leaf += 2;

				while (a > 0 && parent[a-1] != '/') --a;
				if (a) --a;

			}
			if (c == '/') {
				leaf += 3;

				while (a > 0 && parent[a-1] != '/') --a;
				if (a) --a;
				continue;
			}
		}
		break;
	}
	if (a) fprintf(stdout, "/%.*s", a, parent);
	if (*leaf) fprintf(stdout, "/%s", leaf);
}

void send_gophermap(int fd, const char *path) {

	FILE *f;
	char *buffer = 0;
	size_t cap = 0;
	ssize_t len; 

	f = fdopen(fd, "r");
	if (!f) return;

	for(;;) {
		len = getline(&buffer, &cap, f);
		if (len <= 0) break;
		if (buffer[len-1] == '\n') {
			--len;
			buffer[len] = 0;
		}

		/* buckmap standard: 
			text [without tabs] -> sent as informational node
			index - selector, host, port are optional (but tab is required)

		*/

		if (!memchr(buffer, '\t', len)) {
			fprintf(stdout, "i%s", buffer);
		} else {

			char *tabs[4];
			char *cp;

			if (buffer[0] == '.') continue;
			tab_split_4(buffer, tabs);

			// type + user name.
			if (!tabs[0]) continue;
			fprintf(stdout, "%s\t", tabs[0]);

			// selector.
			// if not provided, use file name.
			// if not absolute, make it so.
			cp = tabs[1] ? tabs[1] : tabs[0] + 1;
			print_path(path, cp);

			// host
			fprintf(stdout, "\t%s", tabs[2] ? tabs[2] : host);

			// port
			if (tabs[3]) fprintf(stdout, "\t%s", tabs[3]);
			else fprintf(stdout, "\t%u", port);
		}
		fputs("\r\n", stdout);
	}
	fputs(".\r\n", stdout);
	free(buffer);
	fclose(f);

}

#ifdef USE_SCANDIR
int dir_select(const struct dirent *d) {
	struct stat st;
	mode_t mode;

	if (d->d_name[0] == '.') return 0;
	if (strchr(d->d_name, '\t')) return 0;

	if (stat(d->d_name, &st) < 0) return 0;

	mode = st.st_mode;
	if ((mode & 0444) != 0444) return 0;

	return S_ISDIR(mode) || S_ISREG(mode);
}
#endif

void send_directory(int fd, char *path) {

	struct dirent *d;
	struct stat st;
	int type;
	int gmfd;

#ifdef USE_SCANDIR
	struct dirent **dirents = NULL;
	int count;
#else
	DIR *dp;
#endif


	if (fchdir(fd) < 0) {
		close(fd);
		fail("3file access error");
	}
	// TODO - consider .banner support ; read and send as 'i' lines.

	gmfd = open("gophermap", O_RDONLY|O_NONBLOCK);
	if (gmfd >= 0) {
		if ((fstat(gmfd, &st) == 0) && S_ISREG(st.st_mode) && ((st.st_mode & 0444) == 0444)) {
			send_gophermap(gmfd, path);
			close(fd);
			return;
		}
	}

#ifdef USE_SCANDIR
	count = scandir(".", &dirents, dir_select, alphasort);
	if (count < 0) {
		fail("3file access error");
	} else {
		int i;
		for (i = 0; i < count; ++i) {
			d = dirents[i];

			if (stat(d->d_name, &st) < 0) continue;
			type = classify(&st, d->d_name);
			if (type < 0) continue;

			fprintf(stdout, "%c%s\t", type, d->d_name);

			if (*path)
				fprintf(stdout, "/%s/%s", path, d->d_name);
			else
				fprintf(stdout, "/%s", d->d_name);

			fprintf(stdout,"\t%s\t%d\r\n", host, port);
		}
	}
	free(dirents);
#else
	dp = fdopendir(fd);
	if (dp == NULL) {
		fail("3file access error");
	} else {
		for(;;) {
			d = readdir(dp);
			if (!d) break;
			if (d->d_name[0] == '.') continue;
			if (strchr(d->d_name, '\t')) continue;

			if (stat(d->d_name, &st) < 0) continue;
			type = classify(&st, d->d_name);
			if (type < 0) continue;

			fprintf(stdout, "%c%s\t", type, d->d_name);

			if (*path)
				fprintf(stdout, "/%s/%s", path, d->d_name);
			else
				fprintf(stdout, "/%s", d->d_name);

			fprintf(stdout,"\t%s\t%d\r\n", host, port);
		}
		closedir(dp);
	}
#endif
	fputs(".\r\n", stdout);
}

void usage(int rv) {
	fputs("gopherd [-h hostname] [-p port] [root directory]\n", stdout);
	exit(rv);
}

int parse_port(const char *cp) {
	int rv = 0;
	if (!cp) return 0;

	for(;;) {
		char c = *cp++;
		if (!c) return rv;
		if (c < '0' || c > '9') break;
		rv *= 10;
		rv += c - '0';
		if (rv > 65535) break;
	}
	return 0;
}

char *xgethostname(void) {
	/* gethostname() will not be null terminated if it's too big for the buffer */
	static char buffer[64];
	if (gethostname(buffer, sizeof(buffer)) < 0) return NULL;
	if (!memchr(buffer, 0, sizeof(buffer))) return NULL;
	return buffer;
}

/* set the host/port variables */
void whoami(int fd) {

	struct sockaddr_storage sa;
	socklen_t len = sizeof(sa);

	if (!port) port = parse_port(getenv("TCPLOCALPORT"));
	if (!host) host = getenv("TCPLOCALIP");

	if (!host && getsockname(fd, (struct sockaddr *)&sa, &len) == 0 && sa.ss_family == PF_INET) {
		static char buffer[4*4];

		struct sockaddr_in *sin = (struct sockaddr_in *)&sa;

		host = inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer));
	}

	if (!port) port = 70;
	if (!host) host = xgethostname();
	if (!host) host = "localhost";
}


/* gopherd [options] rootpath */
int main(int argc, char **argv) {

	char buffer[4096];
	struct stat st;

	char *cp;
	int c;
	int type;
	int fd;

	while ((c = getopt(argc, argv, "p:h:")) != -1) {
		switch(c) {
		case 'p':
			port = parse_port(optarg);
			break;
		case 'h':
			host = optarg;
			break;
		default:
			usage(1);
			break;
		}
	}
	argc -= optind;
	argv += optind;

	whoami(STDIN_FILENO);

	cp = fgets(buffer, sizeof(buffer), stdin);
	if (cp) {
		int size = strlen(cp);
		while (size && (cp[size-1] == '\r' || cp[size-1] == '\n')) --size;
		while (size && cp[size-1] == '/') --size;
		cp[size] = 0;

		while (*cp == '/') {
			++cp;
			--size;
		}
	}


	if (!cp) cp = "";

	if (check_path(cp) < 0) {
		fail("3bad selector");
	}

	if (argc) {
		char *root = argv[0];
		if (chdir(root) < 0) {
			fail("3bad root directory");
		}
	}

	fd = open(*cp ? cp : ".", O_RDONLY|O_NONBLOCK);
	if (fd < 0) {
		fail("3file access error");
	}

	if (fstat(fd, &st) < 0) {
		close(fd);
		fail("3file access error");
	}

	type = classify(&st, cp);
	if (type < 0) {
		close(fd);
		fail("3file access error");
	}

	switch(type) {
	case '1':
		send_directory(fd, cp);
		break;
	case '0':
		send_text(fd);
		break;
	default:
		send_binary(fd);
		break;
	}
	exit(0);
}

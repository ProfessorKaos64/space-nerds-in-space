#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

void clean_spaces(char *line)
{
	char *s, *d;
	int skip_spaces = 1;

	s = line;
	d = line;

	while (*s) {
		if ((*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') && skip_spaces) {
			s++;
			continue;
		}
		skip_spaces = 0;
		if (*s == '\t' || *s == '\n' || *s == '\r')
			*s = ' ';
		if (*s == ' ')
			skip_spaces = 1;
		*d = *s;
		s++;
		d++;
	}
	*d = '\0';
}

void remove_trailing_whitespace(char *s)
{
	int len = strlen(s) - 1;

	do {
		switch (s[len]) {
		case '\t':
		case ' ':
		case '\r':
		case '\n':
			s[len] = '\0';
			len--;
		default:
			return;
		}
	} while (1);
}

void uppercase(char *w)
{
	char *i;

	for (i = w; *i; i++)
		*i = toupper(*i);
}

void lowercase(char *w)
{
	char *i;

	for (i = w; *i; i++)
		*i = tolower(*i);
}

char *dir_name(char *path)
{
	char *x;
	int l;

	x = strdup(path);
	if (!x)
		return NULL;
	l = strlen(x);
	while (l > 0 && x[l - 1] != '/') {
		x[l - 1] = '\0';
		l--;
	}
	return x;
}

char *trim_whitespace(char *s)
{
	char *x, *z;

	for (x = s; *x == ' ' || *x == '\t'; x++)
		;
	z = x + (strlen(x) - 1);

	while (z >= x && (*z == ' ' ||  *z == '\t' || *z == '\n')) {
		*z = '\0';
		z--;
	}
	return x;
}

char *skip_leading_whitespace(char *s)
{
	char *i;

	for (i = s; *i == ' ' || *i == '\t' || *i == '\n';)
		i++;
	return i;
}

int has_prefix(char *prefix, char *str)
{
	return strncmp(prefix, str, strlen(prefix)) == 0;
}

char *slurp_file(const char *path, int *bytes)
{
	int rc, fd;
	struct stat statbuf;
	char *buffer;
	int bytesleft, bytesread, count;

	rc = stat(path, &statbuf);
	if (rc) {
		fprintf(stderr, "stat(%s): %s\n", path, strerror(errno));
		return NULL;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s): %s\n", path, strerror(errno));
		return NULL;
	}
	buffer = malloc(statbuf.st_size + 1);

	count = 0;
	bytesread = 0;
	bytesleft = (int) statbuf.st_size;
	do {
		bytesread = read(fd, buffer + count, bytesleft);
		if (bytesread < 0) {
			if (errno == EINTR)
				continue;
			else {
				fprintf(stderr, "read: %d bytes from %s failed: %s\n",
						bytesleft, path, strerror(errno));
				close(fd);
				free(buffer);
				return NULL;
			}
		} else {
			bytesleft -= bytesread;
			count += bytesread;
		}
	} while (bytesleft > 0);
	close(fd);
	buffer[statbuf.st_size] = '\0';
	if (bytes)
		*bytes = (int) statbuf.st_size;
	return buffer;
}

void remove_single_quotes(char *s)
{
	char *src = s;
	char *dest = s;

	do {
		if (*src == '\'') {
			src++;
			continue;
		}
		if (dest == src) {
			if (!*src)
				break;
			dest++;
			src++;
			continue;
		}
		*dest = *src;
		if (!*src)
			break;
		dest++;
		src++;
	} while (1);
}

int strchrcount(char *s, int c)
{
	char *x;
	int count = 0;

	for (x = s; *x; x++)
		if (*x == c)
			count++;
	return count;
}

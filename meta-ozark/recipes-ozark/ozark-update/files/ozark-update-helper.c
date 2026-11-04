/*
 * The parts of ozark-update a shell script can't do well:
 *
 *   ozark-update-helper progress
 *       Follows SWUpdate's progress socket and prints, one per line:
 *         progress N        overall percent (the system image is most of it)
 *         step TEXT         what is being installed
 *         result success|failure [TEXT]
 *       Waits for SWUpdate to start; exits after the result.
 *   ozark-update-helper feed FILE
 *       Reads an update feed (JSON) and prints its fields as key=value lines:
 *       version, build, url, sha256, size, notes (newlines as \n), machine.
 *   ozark-update-helper newer VERSION BUILD THAN_VERSION THAN_BUILD
 *       Exits 0 if VERSION/BUILD is newer (version first, then build).
 *   ozark-update-helper swu-version FILE
 *       Prints version= and description= from an update's sw-description.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <json-c/json.h>
#include <progress_ipc.h>

static int progress(void)
{
	int fd = progress_ipc_connect(true);
	struct progress_msg msg;
	char image[sizeof(msg.cur_image) + 1] = "";
	int last = -1;

	if (fd < 0)
		return 1;
	setvbuf(stdout, NULL, _IOLBF, 0);
	for (;;) {
		int got = progress_ipc_receive(&fd, &msg);
		if (got == 0)
			continue;  /* interrupted */
		if (got < 0) {
			if (fd >= 0)
				close(fd);
			fd = progress_ipc_connect(true);
			if (fd < 0)
				return 1;
			continue;
		}
		switch (msg.status) {
		case START:
			printf("step Checking the update\n");
			break;
		case RUN:
		case PROGRESS: {
			if (!msg.cur_image[0])
				break;
			if (strncmp(image, msg.cur_image, sizeof(msg.cur_image))) {
				memcpy(image, msg.cur_image, sizeof(msg.cur_image));
				image[sizeof(msg.cur_image)] = '\0';
				printf("step %s\n", strstr(image, ".ext4") ? "Writing the system" : "Writing the boot files");
			}
			/* The system image is ~all of the data; the boot files and scripts are quick */
			unsigned percent = msg.cur_percent > 100 ? 100 : msg.cur_percent;
			int overall = strstr(image, ".ext4") ? (int)(percent * 95 / 100) : 95 + (int)(percent * 4 / 100);
			if (overall > last) {
				printf("progress %d\n", overall);
				last = overall;
			}
			break;
		}
		case SUCCESS:
			printf("progress 100\nresult success\n");
			return 0;
		case FAILURE: {
			unsigned len = msg.infolen < sizeof(msg.info) ? msg.infolen : sizeof(msg.info) - 1;
			msg.info[len] = '\0';
			for (char *c = msg.info; *c; c++)
				if (*c == '\n' || *c == '\r')
					*c = ' ';
			printf("result failure %s\n", msg.info);
			return 0;
		}
		default:
			break;
		}
	}
}

static void print_string(json_object *root, const char *key)
{
	json_object *value;
	if (!json_object_object_get_ex(root, key, &value))
		return;
	const char *s = json_object_get_string(value);
	printf("%s=", key);
	for (; s && *s; s++) {
		if (*s == '\n')
			fputs("\\n", stdout);
		else if (*s != '\r')
			putchar(*s);
	}
	putchar('\n');
}

static int feed(const char *path)
{
	json_object *root = json_object_from_file(path);
	if (!root || !json_object_is_type(root, json_type_object)) {
		fprintf(stderr, "The update feed isn't valid JSON\n");
		return 1;
	}
	json_object *version;
	if (!json_object_object_get_ex(root, "version", &version)) {
		fprintf(stderr, "The update feed has no version\n");
		return 1;
	}
	const char *keys[] = {"version", "build", "url", "sha256", "size", "notes", "machine"};
	for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		print_string(root, keys[i]);
	json_object_put(root);
	return 0;
}

static int newer(const char *version, const char *build, const char *than_version, const char *than_build)
{
	int c = strverscmp(version, than_version);
	if (c == 0)
		c = strverscmp(build, than_build);
	return c > 0 ? 0 : 1;
}

/* sw-description is the first file of a .swu (cpio, "newc"/"crc" format) */
static int swu_version(const char *path)
{
	FILE *f = fopen(path, "rb");
	char header[110];
	if (!f || fread(header, 1, sizeof(header), f) != sizeof(header) || strncmp(header, "07070", 5)) {
		fprintf(stderr, "Not an update file: %s\n", path);
		return 1;
	}
	char field[9] = "";
	memcpy(field, header + 6 + 6 * 8, 8);
	unsigned long filesize = strtoul(field, NULL, 16);
	memcpy(field, header + 6 + 11 * 8, 8);
	unsigned long namesize = strtoul(field, NULL, 16);
	unsigned long data = (sizeof(header) + namesize + 3) & ~3UL;
	if (filesize == 0 || filesize > 1 << 20 || fseek(f, (long)data, SEEK_SET)) {
		fprintf(stderr, "Not an update file: %s\n", path);
		return 1;
	}
	char *text = calloc(1, filesize + 1);
	if (!text || fread(text, 1, filesize, f) != filesize) {
		fprintf(stderr, "Can't read %s\n", path);
		return 1;
	}
	fclose(f);

	/* version = "0.2.0"; description = "..."; (the first of each) */
	const char *keys[] = {"version", "description"};
	for (size_t i = 0; i < 2; i++) {
		for (char *p = text; (p = strstr(p, keys[i])); p++) {
			char *q = p + strlen(keys[i]);
			while (*q == ' ' || *q == '\t')
				q++;
			if (*q != '=' && *q != ':')
				continue;
			q++;
			while (*q == ' ' || *q == '\t')
				q++;
			if (*q != '"')
				continue;
			char *end = strchr(++q, '"');
			if (!end)
				break;
			printf("%s=%.*s\n", keys[i], (int)(end - q), q);
			break;
		}
	}
	free(text);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "progress"))
		return progress();
	if (argc == 3 && !strcmp(argv[1], "feed"))
		return feed(argv[2]);
	if (argc == 6 && !strcmp(argv[1], "newer"))
		return newer(argv[2], argv[3], argv[4], argv[5]);
	if (argc == 3 && !strcmp(argv[1], "swu-version"))
		return swu_version(argv[2]);
	fprintf(stderr, "usage: %s progress | feed FILE | newer V B THAN_V THAN_B | swu-version FILE\n", argv[0]);
	return 2;
}

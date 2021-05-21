#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "zdtmtst.h"

const char *test_doc	= "Check that a fifo read-only descriptor is restored with data";
const char *test_author	= "Andrew Vagin <avagin@openvz.org>";

char *filename;
TEST_OPTION(filename, string, "file name", 1);

#define BUF_SIZE (16 * 4096) /* A fifo buffer has 16 slots by default */

int main(int argc, char **argv)
{
	int fd;
	int fd_ro;
	struct stat st;
	mode_t mode = S_IFIFO | 0700;
	uint8_t buf[BUF_SIZE];
	uint32_t crc;
	int ret;;

	test_init(argc, argv);

	if (mknod(filename, mode, 0)) {
		err("can't make fifo \"%s\": %m\n", filename);
		exit(1);
	}

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		err("can't open %s: %m\n", filename);
		return 1;
	}

	fd_ro = open(filename, O_RDONLY);
	if (fd_ro < 0) {
		err("can't open %s: %m\n", filename);
		return 1;
	}

	crc = ~0;
	datagen(buf, BUF_SIZE, &crc);
	ret = write(fd, buf, BUF_SIZE);
	if (ret != BUF_SIZE) {
		err("write() failed\n");
		return 1;
	}

	close(fd);

	test_daemon();
	test_waitsig();

	ret = read(fd_ro, buf, BUF_SIZE);
	if (ret != BUF_SIZE) {
		err("read() failed\n");
		return 1;
	}

	crc = ~0;
	if (datachk(buf, BUF_SIZE, &crc)) {
		fail("data corrupted\n");
		return 1;
	}

	if (close(fd_ro) < 0) {
		fail("can't close %s: %m", filename);
		return 1;
	}

	if (stat(filename, &st) < 0) {
		fail("can't stat %s: %m", filename);
		return 1;
	}

	if (st.st_mode != mode) {
		fail("%s is no longer the fifo we had", filename);
		return 1;
	}

	if (unlink(filename) < 0) {
		fail("can't unlink %s: %m", filename);
		return 1;
	}

	pass();
	return 0;
}

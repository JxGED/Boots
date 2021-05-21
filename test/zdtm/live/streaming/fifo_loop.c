#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "zdtmtst.h"

const char *test_doc	= "Multi-process fifo loop";
#define BUF_SIZE	256
#define PROCS_DEF	4
unsigned int num_procs = PROCS_DEF;
TEST_OPTION(num_procs, uint, "# processes to create "
	"(default " __stringify(PROCS_DEF) ")", 0);
char *filename;
TEST_OPTION(filename, string, "file name", 1);

static int pids[PROCS_DEF];

volatile sig_atomic_t num_exited = 0;
void inc_num_exited(int signo)
{
	num_exited++;
}

int main(int argc, char **argv)
{
	int ret = 0;
	int readfd, writefd;
	mode_t mode = S_IFIFO | 0644;
	char path[PROCS_DEF][BUF_SIZE];
	pid_t pid;
	int i;
	uint8_t buf[0x100000];
	char *file_path;

	test_init(argc, argv);

	for (i = 0; i < PROCS_DEF; i++) {
		file_path = path[i];
		if (snprintf(file_path, BUF_SIZE, "%s-%02d", filename, i) >= BUF_SIZE) {
			err("filename %s is too long\n", filename);
			exit(1);
		}
		if (mkfifo(file_path, mode)) {
			err("can't make fifo \"%s\": %m\n", file_path);
			exit(1);
		}
	}

	if (signal(SIGCHLD, inc_num_exited) == SIG_ERR) {
		err("can't set SIGCHLD handler: %m\n");
		exit(1);
	}

	for (i = 1; i < num_procs; i++) {	/* i = 0 - parent */
		pid = test_fork();
		if (pid < 0) {
			err("Can't fork: %m\n");
			kill(0, SIGKILL);
			exit(1);
		}
		if (pid == 0) {
			file_path = path[i - 1];
			readfd = open(file_path, O_RDONLY);
			if (readfd < 0) {
				err("open(%s, O_RDONLY) Failed: %m\n",
					file_path);
				ret = errno;
				return ret;
			}
			file_path = path[i];
			writefd = open(file_path, O_WRONLY);
			if (writefd < 0) {
				err("open(%s, O_WRONLY) Failed: %m\n",
					file_path);
				ret = errno;
				return ret;
			}
			signal(SIGPIPE, SIG_IGN);
			if (pipe_in2out(readfd, writefd, buf, sizeof(buf)) < 0)
				/* pass errno as exit code to the parent */
				if (test_go() /* signal NOT delivered */ ||
						(errno != EINTR && errno != EPIPE))
					ret = errno;
			close(readfd);
			close(writefd);
			exit(ret);
		}
		pids[i] = pid;
	}

	file_path = path[0];
	writefd = open(file_path, O_WRONLY);
	if (writefd < 0) {
		err("open(%s, O_WRONLY) Failed: %m\n", file_path);
		kill(0, SIGKILL);
		exit(1);
	}

	file_path = path[i - 1];
	readfd = open(file_path, O_RDONLY);
	if (readfd < 0) {
		err("open(%s, O_RDONLY) Failed: %m\n", file_path);
		kill(0, SIGKILL);
		exit(1);
	}

	if (num_exited) {
		err("Some children died unexpectedly\n");
		kill(0, SIGKILL);
		exit(1);
	}

	test_daemon();

	while (test_go()) {
		int len, rlen = 0, wlen;
		uint8_t rbuf[sizeof(buf)], *p;

		datagen(buf, sizeof(buf), NULL);
		wlen = write(writefd, buf, sizeof(buf));
		if (wlen < 0) {
			if (errno == EINTR)
				continue;
			else {
				fail("write failed: %m\n");
				ret = 1;
				break;
			}
		}

		for (p = rbuf, len = wlen; len > 0; p += rlen, len -= rlen) {
			rlen = read(readfd, p, len);
			if (rlen <= 0)
				break;
		}

		if (rlen < 0 && errno == EINTR)
			continue;

		if (len > 0) {
			fail("read failed: %m\n");
			ret = 1;
			break;
		}

		if (memcmp(buf, rbuf, wlen)) {
			fail("data mismatch\n");
			ret = 1;
			break;
		}
	}

	close(writefd);

	test_waitsig(); /* even if failed, wait for migration to complete */

	if (kill(0, SIGTERM)) {
		fail("failed to send SIGTERM to my process group: %m\n");
		return 1;	/* shouldn't wait() in this case */
	}
	close(readfd);

	for (i = 1; i < num_procs; i++) {	/* i = 0 - parent */
		int chret;
		if (waitpid(pids[i], &chret, 0) < 0) {
			fail("waitpid error: %m\n");
			ret = 1;
			continue;
		}

		chret = WEXITSTATUS(chret);
		if (chret) {
			fail("child %d exited with non-zero code %d (%s)\n",
				i, chret, strerror(chret));
			ret = 1;
			continue;
		}
	}

	if (!ret)
		pass();

	for (i = 0; i < PROCS_DEF; i++)
		unlink(path[i]);
	return 0;
}
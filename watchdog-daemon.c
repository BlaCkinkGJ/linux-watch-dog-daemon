#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <syslog.h>
#include <setjmp.h>
#include <utmp.h>
#include <string.h>
#include <time.h>

#define BUF_SIZE 1024
#define MAX_UTMP_LINE 1024

jmp_buf jump_buffer;

static void 
skeleton_daemon() {
	pid_t pid;

	pid = fork();

	if (pid < 0)
		exit(EXIT_FAILURE);

	if (pid > 0)
		exit(EXIT_SUCCESS);

	if (setsid() < 0)
		exit(EXIT_FAILURE);

	// Catch, ignore and handle signals.
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	pid = fork();

	if (pid < 0)
		exit(EXIT_FAILURE);

	if (pid > 0)
		exit(EXIT_SUCCESS);

	umask(0);

	chdir("/tmp");
}

int make_tmp_file(char *);
char *make_tmp_dir(char *);
bool utmp_cmp(struct utmp*, struct utmp*);
int utmp_watch(int, struct utmp*);


int
main() {
	char tmp_dir[] = "/tmp/cseXXXXXX";
	char *dir = NULL;
	char tmp_file[PATH_MAX];
	struct utmp utmp_buffer[MAX_UTMP_LINE];

	int tmp_fd = -1;
	int utmp_fd = -1;

	skeleton_daemon();

	// add the exception handler
	if (setjmp(jump_buffer) != 0) {
		syslog (LOG_ERR, "Critical Error Detected. Close...");
		closelog();
		if (tmp_fd != -1) { // temporary file unlink
			close(tmp_fd);
			syslog (LOG_INFO, "Unlink file");
			unlink(tmp_file);
		}
		if (dir != NULL) { // temporary folder unlink
			syslog (LOG_INFO, "Remove directory");
			rmdir(dir);
		}
		if (!(utmp_fd < 0)) {
			close(utmp_fd);
		}
		return EXIT_FAILURE;
	}

	dir = make_tmp_dir(tmp_dir);
	sprintf(tmp_file, "%s/cseXXXXXX", dir);
	tmp_fd = make_tmp_file(tmp_file);

	int counter = 10;
	while (counter--) {
		syslog (LOG_NOTICE, "First daemon started.");
		utmp_fd = utmp_watch(tmp_fd, utmp_buffer);
		sleep(5);
	}

	syslog (LOG_NOTICE, "First daemon terminated.");
	// file close
	closelog();
	close(utmp_fd);
	close(tmp_fd);
	// tmp file and folder deletion
	unlink(tmp_file);
	syslog (LOG_INFO, "Unlink file");
	if (rmdir(dir) == -1) {
		longjmp(jump_buffer, -1);
	}
	syslog (LOG_INFO, "Unlink directory");

	return EXIT_SUCCESS;
}

bool utmp_cmp(struct utmp *src, struct utmp *dst) {
	bool is_same_line = strcmp(src->ut_line, dst->ut_line) == 0;
	bool is_same_user = strcmp(src->ut_user, dst->ut_user) == 0;
	bool is_same_id = strcmp(src->ut_id, dst->ut_id) == 0;
	bool is_same_host = strcmp(src->ut_host, dst->ut_host) == 0;
	syslog(LOG_INFO, "%s %s", src->ut_user, dst->ut_user);
	return is_same_host && is_same_id && is_same_line && is_same_user;
}

bool is_same_exist(struct utmp *list, struct utmp src) {
	int idx = 0;
	for(idx = 0; idx < MAX_UTMP_LINE; idx++) {
		if (utmp_cmp(&list[idx], &src) == true) {
			return true;
		}
	}
	return false;
}

static char *get_utmp_time(struct utmp _utmp) {
	int64_t seconds = _utmp.ut_tv.tv_sec;
	return ctime((const time_t *)&(seconds));
}

int utmp_watch(int tmp_fd, struct utmp *utmp_list) {
	static int utmp_fd = -1;
	static int counter = 0;
	static bool is_draw_header = true;

	int idx = 0;
	char tmp_str[BUF_SIZE] = "\0";
	if(utmp_fd < 0) { 
		if ((utmp_fd = open(_PATH_UTMP, O_RDONLY)) < 0) {
			syslog(LOG_ERR, "File open error");
			longjmp(jump_buffer, -1);
		}
	}

	if (is_draw_header == true) { 
		sprintf(tmp_str, "state     user name     last access time\n");
		strcat(tmp_str, "=========================================\n");
		write(tmp_fd, tmp_str, strlen(tmp_str));
		is_draw_header = false;
	}
	struct utmp temp;
	for(idx = 0; read(utmp_fd, &temp, sizeof(struct utmp)) > 0; idx++) {
		if (is_same_exist(utmp_list, temp) == false) {
			memcpy(&utmp_list[counter++], &temp, sizeof(struct utmp));

			sprintf(tmp_str, "login     %-9s     %s",temp.ut_user, get_utmp_time(temp));
			write(tmp_fd, tmp_str, strlen(tmp_str));
		}
	}
	lseek(utmp_fd, 0, SEEK_SET);
	return utmp_fd;
}

char *make_tmp_dir(char *template) {
	char *name;

	if ((name = mkdtemp(template)) == NULL) {
		syslog(LOG_ERR, "Can't create temp directory");
		longjmp(jump_buffer, -1);
	}
	syslog(LOG_INFO, "temp dir name = %s\n", template);
	return name;
}

int make_tmp_file(char *template) {
	int fd;

	if ((fd = mkstemp(template)) < 0) {
		syslog(LOG_ERR, "Can't create temp file");
		longjmp(jump_buffer, -1);
	}
	syslog(LOG_INFO, "temp file name = %s\n", template);
	return fd;
}

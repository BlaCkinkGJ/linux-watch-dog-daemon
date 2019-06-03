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

static jmp_buf jump_buffer;
static time_t startup_time;
static int counter; // list buffer counter


struct user_table {
	pid_t pid;
	char name[UT_NAMESIZE];
};

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
int wtmp_watch(int, struct utmp*);


int
main() {
	char tmp_dir[] = "/tmp/cseXXXXXX";
	char *dir = NULL;
	char tmp_file[PATH_MAX];
	struct utmp utmp_buffer[MAX_UTMP_LINE];

	int tmp_fd = -1;
	int utmp_fd = -1;
	int wtmp_fd = -1;

	skeleton_daemon();
	startup_time = time(NULL);

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
		if (!(wtmp_fd < 0)) {
			close(wtmp_fd);
		}
		return EXIT_FAILURE;
	}

	dir = make_tmp_dir(tmp_dir);
	sprintf(tmp_file, "%s/cseXXXXXX", dir);
	tmp_fd = make_tmp_file(tmp_file);

	int counter = 14;
	do {
		syslog (LOG_NOTICE, "First daemon started.");
		utmp_fd = utmp_watch(tmp_fd, utmp_buffer);
		wtmp_fd = wtmp_watch(tmp_fd, utmp_buffer);
		sleep(5);
	} while (counter--);

	syslog (LOG_NOTICE, "First daemon terminated.");
	// file close
	closelog();
	close(utmp_fd);
	close(wtmp_fd);
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
	bool is_same_user = strcmp(src->ut_user, dst->ut_user) == 0;
	bool is_same_time = src->ut_tv.tv_usec == dst->ut_tv.tv_usec;
	return is_same_user && is_same_time;
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
	static bool is_draw_header = true;

	int idx = 0;
	char tmp_str[BUF_SIZE] = "\0";

	struct utmp temp;

	if(counter >= MAX_UTMP_LINE) {
		syslog(LOG_ERR, "MAX UTMP buffer reached");
		longjmp(jump_buffer, -1);
	}
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
	for(idx = 0; read(utmp_fd, &temp, sizeof(struct utmp)) > 0; idx++) {
		if (is_same_exist(utmp_list, temp) == false && temp.ut_type == USER_PROCESS && temp.ut_tv.tv_sec > startup_time) {
			memcpy(&utmp_list[counter++], &temp, sizeof(struct utmp));

			sprintf(tmp_str, "login     %-9s     %s",temp.ut_user, get_utmp_time(temp));
			write(tmp_fd, tmp_str, strlen(tmp_str));
		}
	}
	lseek(utmp_fd, 0, SEEK_SET);
	return utmp_fd;
}

const char* get_wtmp_user(struct user_table* tab, pid_t pid) {
	int idx = 0;
	for (idx = 0; idx < MAX_UTMP_LINE; idx++) {
		if (tab[idx].pid == pid) {
			return tab[idx].name;
		}
	}
	return "\0";
}

int wtmp_watch(int tmp_fd, struct utmp *utmp_list) {
	static int wtmp_fd = -1;
	// because of the utmp already draw the header
	static bool is_draw_header = false;

	int idx = 0;
	char tmp_str[BUF_SIZE] = "\0";

	struct utmp temp;
	struct user_table wtmp_user[MAX_UTMP_LINE];

	if(counter >= MAX_UTMP_LINE) {
		syslog(LOG_ERR, "MAX UTMP buffer reached");
		longjmp(jump_buffer, -1);
	}

	if(wtmp_fd < 0) { 
		if ((wtmp_fd = open(_PATH_WTMP, O_RDONLY)) < 0) {
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

	for(idx = 0; read(wtmp_fd, &temp, sizeof(struct utmp)) > 0; idx++) {
		wtmp_user[idx].pid = temp.ut_pid;
		strcpy(wtmp_user[idx].name, temp.ut_user);
	}
	lseek(wtmp_fd, 0, SEEK_SET);

	for(idx = 0; read(wtmp_fd, &temp, sizeof(struct utmp)) > 0; idx++) {
		if (is_same_exist(utmp_list, temp) == false && temp.ut_type == DEAD_PROCESS && temp.ut_tv.tv_sec > startup_time) {
			syslog(LOG_INFO, "==== SELECTED ====");
			memcpy(&utmp_list[counter++], &temp, sizeof(struct utmp));

			sprintf(tmp_str, "logout    %-9s     %s", get_wtmp_user(wtmp_user, temp.ut_pid), get_utmp_time(temp));
			write(tmp_fd, tmp_str, strlen(tmp_str));
		}
	}
	lseek(wtmp_fd, 0, SEEK_SET);
	return wtmp_fd;
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

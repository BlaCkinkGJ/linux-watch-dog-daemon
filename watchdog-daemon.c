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

#define BUF_SIZE 1024           // regular string buffer size
#define MAX_UTMP_LINE 5120      // maximum number of utmp list

static jmp_buf jump_buffer;     // exception handler
static time_t startup_time;     // contain the program startup time
static int counter;             // counter of 
static char command[BUF_SIZE];

/**
 * @brief This struct for pid to name changing.
 * This is similar to key-value data structure.
 */
struct user_table {
	pid_t pid;
	char name[UT_NAMESIZE];
};

/**
 * @brief This function for skeleton.
 * It control the daemon's exit status.
 * Exactly, this is parent of daemon.
 */
static void skeleton_daemon() {
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

/**
 * @brief Use the linux command wall.
 * This can broadcast of the temporary file contents
 */
void wall_the_file() {
	if (strlen(command) > 0)
		system(command);
	alarm(30);
}

/**
 * @brief This is the main part of the daemon.
 * Consist of 7th part.
 * First, build the skeleton of daemon
 * Second, registry the signal
 * Third, registry the exception handler
 * Fourth, make the temporary directory and file
 * Fifth, make wall command
 * Sixth, watch the utmp and wtmp file.
 * Seventh, destructor of this file
 * @return int 
 */
int main() {
	char tmp_dir[] = "/tmp/cseXXXXXX";
	char *dir = NULL;
	char tmp_file[PATH_MAX];
	struct utmp utmp_buffer[MAX_UTMP_LINE];

	int tmp_fd = -1;
	int utmp_fd = -1;
	int wtmp_fd = -1;

	// build the skeleton of daemon
	skeleton_daemon();
	
	// registry the signal
	signal(SIGALRM, wall_the_file);
	alarm(30);
	startup_time = time(NULL);

	// registry exception handler
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

	// make temporary directory and file
	dir = make_tmp_dir(tmp_dir);
	sprintf(tmp_file, "%s/cseXXXXXX", dir);
	tmp_fd = make_tmp_file(tmp_file);

	// make wall command
	sprintf(command, "wall < %s", tmp_file);
	while(1) {
		syslog (LOG_NOTICE, "daemon started.");
		utmp_fd = utmp_watch(tmp_fd, utmp_buffer);
		wtmp_fd = wtmp_watch(tmp_fd, utmp_buffer);
		sleep(1);
	}

	// destrutor of this file
	syslog (LOG_NOTICE, "daemon terminated.");
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

/**
 * @brief compare the utmp contents.
 * in our case, I check the only user and time
 * 
 * @param src 
 * @param dst 
 * @return true 
 * @return false 
 */
bool utmp_cmp(struct utmp *src, struct utmp *dst) {
	bool is_same_user = strcmp(src->ut_user, dst->ut_user) == 0;
	bool is_same_time = src->ut_tv.tv_usec == dst->ut_tv.tv_usec;
	return is_same_user && is_same_time;
}

/**
 * @brief comapre src utmp struct to __all__ list data
 * 
 * @param list 
 * @param src 
 * @return true 
 * @return false 
 */
bool is_same_exist(struct utmp *list, struct utmp src) {
	int idx = 0;
	for(idx = 0; idx < MAX_UTMP_LINE; idx++) {
		if (utmp_cmp(&list[idx], &src) == true) {
			return true;
		}
	}
	return false;
}

/**
 * @brief Get the utmp time object.
 * And time object is formatted by ctime function
 * 
 * @param _utmp 
 * @return char* 
 */
static char *get_utmp_time(struct utmp _utmp) {
	int64_t seconds = _utmp.ut_tv.tv_sec;
	return ctime((const time_t *)&(seconds));
}

/**
 * @brief utmp file watchdog function
 * 
 * @param tmp_fd 
 * @param list 
 * @return int 
 */
int utmp_watch(int tmp_fd, struct utmp *list) {
	static int utmp_fd = -1;
	static bool is_draw_header = true;

	int idx = 0;
	char tmp_str[BUF_SIZE] = "\0";

	struct utmp temp;

	// open the utmp file
	if(utmp_fd < 0) { 
		if ((utmp_fd = open(_PATH_UTMP, O_RDONLY)) < 0) {
			syslog(LOG_ERR, "File open error");
			longjmp(jump_buffer, -1);
		}
	}

	// draw the header of the temporary file
	if (is_draw_header == true) { 
		sprintf(tmp_str, "state     user name     last access time\n");
		strcat(tmp_str, "=========================================\n");
		write(tmp_fd, tmp_str, strlen(tmp_str));
		is_draw_header = false;
	}

	// check the entry which updates
	for(idx = 0; read(utmp_fd, &temp, sizeof(struct utmp)) > 0; idx++) {
		bool is_user_process = temp.ut_type == USER_PROCESS;
		bool is_newer_than_startup = temp.ut_tv.tv_sec > startup_time;
		if (is_same_exist(list, temp) == false && is_user_process && is_newer_than_startup) {
			memcpy(&list[counter++%MAX_UTMP_LINE], &temp, sizeof(struct utmp));

			sprintf(tmp_str, "login     %-9s     %s",temp.ut_user, get_utmp_time(temp));
			write(tmp_fd, tmp_str, strlen(tmp_str));
		}
	}
	// reverse the file's first position
	lseek(utmp_fd, 0, SEEK_SET);
	return utmp_fd;
}

/**
 * @brief Get the wtmp user object to refer the table with pid
 * 
 * @param tab 
 * @param pid 
 * @return const char* 
 */
const char* get_wtmp_user(struct user_table* tab, pid_t pid) {
	int idx = 0;
	for (idx = 0; idx < MAX_UTMP_LINE; idx++) {
		if (tab[idx].pid == pid) {
			return tab[idx].name;
		}
	}
	return "\0";
}

/**
 * @brief wtmp file watchdog function
 * 
 * @param tmp_fd 
 * @param list 
 * @return int 
 */
int wtmp_watch(int tmp_fd, struct utmp *list) {
	static int wtmp_fd = -1;
	// because of the utmp already draw the header
	static bool is_draw_header = false;

	int idx = 0;
	char tmp_str[BUF_SIZE] = "\0";

	struct utmp temp;
	struct user_table wtmp_user[MAX_UTMP_LINE];

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
		bool is_dead_process = temp.ut_type == DEAD_PROCESS;
		bool is_newer_than_startup = temp.ut_tv.tv_sec > startup_time;
		if (is_same_exist(list, temp) == false && is_dead_process && is_newer_than_startup) {
			memcpy(&list[counter++%MAX_UTMP_LINE], &temp, sizeof(struct utmp));

			sprintf(tmp_str, "logout    %-9s     %s", get_wtmp_user(wtmp_user, temp.ut_pid), get_utmp_time(temp));
			write(tmp_fd, tmp_str, strlen(tmp_str));
		}
	}
	lseek(wtmp_fd, 0, SEEK_SET);
	return wtmp_fd;
}

/**
 * @brief make the temporary directory
 * 
 * @param template 
 * @return char* 
 */
char *make_tmp_dir(char *template) {
	char *name;

	if ((name = mkdtemp(template)) == NULL) {
		syslog(LOG_ERR, "Can't create temp directory");
		longjmp(jump_buffer, -1);
	}
	syslog(LOG_INFO, "temp dir name = %s\n", template);
	return name;
}

/**
 * @brief make the temporary file
 * 
 * @param template 
 * @return int 
 */
int make_tmp_file(char *template) {
	int fd;

	if ((fd = mkstemp(template)) < 0) {
		syslog(LOG_ERR, "Can't create temp file");
		longjmp(jump_buffer, -1);
	}
	syslog(LOG_INFO, "temp file name = %s\n", template);
	return fd;
}

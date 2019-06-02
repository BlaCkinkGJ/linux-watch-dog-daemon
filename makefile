CC=gcc
CFLAGS= -Werror -Wall -g
TARGET= watchdog

$(TARGET): watchdog.o
		   $(CC) $(CFLAGS) -o $(TARGET) watchdog.o
watchdog.o: watchdog-daemon.c
		   $(CC) $(CFLAGS) -c -o watchdog.o watchdog-daemon.c

clean:
	rm *.o $(TARGET)

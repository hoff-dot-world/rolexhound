/*  rolexhound.c - filesystem event notify watchdog
    Copyright (C) 2024 hoff.industries

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include <signal.h>
#include <unistd.h>
#include <string.h>

#include <sys/inotify.h>
#include <libnotify/notify.h>

#define EXT_SUCCESS 0
#define EXT_ERR_TOO_FEW_ARGS 1
#define EXT_ERR_INIT_INOTIFY 2
#define EXT_ERR_ADD_WATCH 3
#define EXT_ERR_BASE_PATH_NULL 4
#define EXT_ERR_READ_INOTIFY 5
#define EXT_ERR_INIT_LIBNOTIFY 6

int IeventQueue = -1;
int IeventDescriptor = -1;

char *ProgramTitle = "rolexhound";

/* Catch shutdown signals so we can
 * cleanup exit properly */
void sig_shutdown_handler(int signal) {
	int closeStatus;

	printf("Exit signal received!\nclosing inotify descriptors...\n");

	// don't need to check if they're valid because
	// the signal handlers are registered after they're created;
	// we can assume they are set up correctly.
	closeStatus = inotify_rm_watch(IeventQueue, IeventDescriptor);
	if (closeStatus == -1) {
		fprintf(stderr, "Error removing file from inotify watch!\n");
	}
	close(IeventQueue);

	notify_uninit();
	exit(EXT_SUCCESS);
}

int main(int argc, char **argv) {

	bool notifyInitStatus;

	char buffer[4096];

	char *token = NULL;
	char *basePath = NULL;

	char *notificationMessage = NULL;

	int readLength = 0;

	NotifyNotification *notifyHandle;

	const struct inotify_event* watchEvent;

	const uint32_t watch_flags = IN_CREATE | IN_DELETE |
		IN_ACCESS | IN_CLOSE_WRITE | IN_MODIFY | IN_MOVE_SELF;

	// argv[1] is the filename to watch
	if (argc < 2) {
		fprintf(stderr, "USAGE: %s PATH\n", ProgramTitle);
		exit(EXT_ERR_TOO_FEW_ARGS);
	}

	notifyInitStatus = notify_init (ProgramTitle);

	if (!notifyInitStatus) {
		fprintf(stderr, "Error initialising with libnotify!\n");
		exit(EXT_ERR_INIT_LIBNOTIFY);
	}

	IeventQueue = inotify_init();

	if (IeventQueue == -1) {
		fprintf(stderr, "Error initialising event queue!\n");
		exit(EXT_ERR_INIT_INOTIFY);
	}

	IeventDescriptor = inotify_add_watch(IeventQueue, argv[1], watch_flags);

	if (IeventDescriptor == -1) {
		fprintf(stderr, "Error adding file to inotify watch!\n");
		exit(EXT_ERR_ADD_WATCH);
	}

	// register signal handlers, note we
	// have already checked all error states
	signal(SIGABRT, sig_shutdown_handler);
	signal(SIGINT, sig_shutdown_handler);
	signal(SIGTERM, sig_shutdown_handler);

	// strtok is destructive so make
	// a copy for it to use instead
	basePath = (char *)malloc(sizeof(char)*(strlen(argv[1]) + 1));
	strcpy(basePath, argv[1]);

	token = strtok(basePath, "/");
	while (token != NULL) {
		basePath = token;
		token = strtok(NULL, "/");
	}

	if (basePath == NULL) {
		fprintf(stderr, "Error getting base file path!\n");
		exit(EXT_ERR_BASE_PATH_NULL);
	}

	// daemon main loop
	while (true) {

		// block on inotify event read
		printf("Waiting for ievent...\n");
		readLength = read(IeventQueue, buffer, sizeof(buffer));

		if (readLength == -1) {
			fprintf(stderr, "Error reading from inotify event!\n");
			exit(EXT_ERR_READ_INOTIFY);
		}

		// one read could yield multiple events; loop through all
		// kinda tricky to get at a glance but basically moves the pointer
		// through the buffer according to the length of the inotify_event struct
		// and the length of the previous inotify event data
		for (char *buffPointer = buffer; buffPointer < buffer + readLength;
			 buffPointer += sizeof(struct inotify_event) + watchEvent->len) {

			notificationMessage = NULL;
			watchEvent = (const struct inotify_event *) buffPointer;

			if (watchEvent->mask & IN_CREATE) {
				notificationMessage = "File created.\n";
			}

			if (watchEvent->mask & IN_DELETE) {
				notificationMessage = "File deleted.\n";
			}

			if (watchEvent->mask & IN_ACCESS) {
				notificationMessage = "File accessed.\n";
			}

			if (watchEvent->mask & IN_CLOSE_WRITE) {
				notificationMessage = "File written and closed.\n";
			}

			if (watchEvent->mask & IN_MODIFY) {
				notificationMessage = "File modified.\n";
			}

			if (watchEvent->mask & IN_MOVE_SELF) {
				notificationMessage = "File moved.\n";
			}

			if (notificationMessage == NULL) {
				continue;
			}

			notifyHandle = notify_notification_new(basePath, notificationMessage, "dialog-information");
			if (notifyHandle == NULL) {
				fprintf(stderr, "Got a null notify handle!\n");
				continue;
			}
			notify_notification_set_urgency(notifyHandle, NOTIFY_URGENCY_CRITICAL);
			notify_notification_show(notifyHandle, NULL);
		}
	}
}

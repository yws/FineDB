/**
 * FineDB server
 *
 * @author	Amaury Bouchard <amaury@amaury.net>
 * @copyright	© 2013, Amaury Bouchard
 */
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "nanomsg/nn.h"
#include "nanomsg/fanout.h"
#include "ydefs.h"
#include "ylog.h"
#include "finedb.h"
#include "database.h"
#include "server.h"
#include "connection_thread.h"
#include "writer_thread.h"

/* Global variable used by signal handlers. */
finedb_t *finedb_g;

/* Declaration of private functions. */
static void signal_handler(int signal);

/** Usage function. */
static void usage() {
	printf("Usage: finedb [-t number] [-p port] [-f path] [-h] [-d]\n"
		"\t-t number    Set the number of connection threads.\n"
		"\t-p port      Listening port number.\n"
		"\t-f path      Path to the database directory.\n"
		"\t-h           Shows this help and exits.\n"
		"\t-d           Debug mode. Error messages are more verbose.\n"
		"\n");
}

/** Signal handler. */
static void signal_handler(int sig) {
	YLOG_ADD(YLOG_DEBUG, "Interruption signal catched.");
	signal(sig, SIG_IGN);
	finedb_g->run = YFALSE;
	// close database
	database_close(finedb_g->database);
	// exit program
	exit(0);
}

/**
 * Main function of the program.
 */
int main(int argc, char *argv[]) {
	char *optstr = "dht:f:p:";
	int i;
	unsigned short nbr_threads = DEFAULT_NBR_THREADS;
	unsigned short port = DEFAULT_PORT;
	char *db_path = DEFAULT_DB_PATH;
	finedb_t *finedb;

	// FineDB structure init
	finedb = init_finedb();
	finedb_g = finedb;
	// signal handlers
	signal(SIGINT, signal_handler);
	// log init
	YLOG_INIT_STDERR();
	YLOG_SET_NOTE();
	// parse command line parameters
	while ((i = getopt(argc, argv, optstr)) != -1) {
		switch (i) {
		case 't':
			nbr_threads = (unsigned short)atoi(optarg);
			break;
		case 'p':
			port = (unsigned short)atoi(optarg);
			break;
		case 'f':
			db_path = strdup(optarg);
			break;
		case 'd':
			YLOG_SET_DEBUG();
			break;
		case 'h':
			usage();
			exit(0);
		}
	}
	YLOG_ADD(YLOG_DEBUG, "Configuration\n\tNumber of threads: %d\n"
	         "\tPort number: %d\n\tDatabase path: %s", nbr_threads, port,
	         db_path);
	// open database
	finedb->database = database_open(db_path);
	if (finedb->database == NULL) {
		YLOG_ADD(YLOG_ERR, "Unable to open database.");
		exit(1);
	}
	// create the nanomsg socket for threads communication
	if ((finedb->threads_socket = nn_socket(AF_SP, NN_PUSH)) < 0 ||
	    nn_bind(finedb->threads_socket, ENDPOINT_THREADS_SOCKET) < 0) {
		YLOG_ADD(YLOG_CRIT, "Unable to create threads socket.");
		database_close(finedb->database);
		exit(2);
	}
	// create writer thread
	if (pthread_create(&finedb->writer_tid, NULL, writer_loop, finedb)) {
        	YLOG_ADD(YLOG_ERR, "Unable to create writer thread.");
		database_close(finedb->database);
		exit(3);
	}
	// create connection threads
	for (i = 0; (unsigned short)i < nbr_threads; i++) {
		tcp_thread_t *thread;

		if ((thread = connection_thread_new(finedb)) != NULL)
			yv_add(&finedb->tcp_threads, thread);
	}
	// create the listening socket
	if (create_listening_socket(finedb, port) != YENOERR) {
		YLOG_ADD(YLOG_CRIT, "Aborting.");
		exit(4);
	}
	// main server loop
	main_loop(finedb);

	return (0);
}


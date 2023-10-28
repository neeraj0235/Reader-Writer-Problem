#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/stat.h>

#define lock() pthread_mutex_lock()
#define unlock() pthread_mutex_unlock()
#define CMD_CT 10
#define FILE_CT 20
#define THREAD_CT 10

int TOTAL_CMDS = 0;
int readers[FILE_CT];
int writers[FILE_CT];
bool active_writer[FILE_CT];

char* file_name[FILE_CT];
char** cmds[CMD_CT];

pthread_t th[THREAD_CT];
pthread_mutex_t write_lock[FILE_CT];
pthread_cond_t read_cond[FILE_CT], write_cond[FILE_CT];

typedef struct my_args {
	int read_file_no;
	int write_file_no;
	int cmd_no;
} my_args;


char* input_command(FILE* stream) {
	char* command = NULL;
	size_t buffer_size = 0;

	if (getline(&command, &buffer_size, stream) == -1) {
		free(command);

		feof(stream) ? exit(EXIT_SUCCESS) : exit(EXIT_FAILURE);
	}
	return command;
}

void input() {
	for (int i = 0, j = 0; i < CMD_CT; i++) {
		char* cmd = input_command(stdin);

		if (!strcmp(cmd, "exit"))
			break;

		int argc = (cmd[0] == 'r' ? 2 : 4);
		cmds[i] = (char**)malloc(argc * sizeof(char*));

		char* ptr = strtok(cmd, " ");
		for (int k = 0; k < argc; k++) {
			cmds[i][k] = ptr;
			if (k == argc - 2) {
				ptr = strtok(NULL, "\n");
				ptr[strlen(ptr) - 1] = '\0';
			} else
				ptr = strtok(NULL, " ");
		}

		if (!strcmp(cmds[i][0], "read"))
			file_name[j++] = strdup(cmds[i][1]);
		else {
			file_name[j++] = strdup(cmds[i][2]);
			if (!strcmp(cmds[i][1], "1"))
				file_name[j++] = strdup(cmds[i][3]);
		}
		TOTAL_CMDS++;
		// THREAD_CT++;
		// FILE_CT += (cmds[i][0] == 'read' ? 1 : (cmds[i][1] == '1' ? 2 : 1));
	}
}


void* my_read(void* args) {
	// printf("Hi from reader!\n");
	int c = ((my_args*)args)->cmd_no,
	    r = ((my_args*)args)->read_file_no;

	pthread_mutex_lock(&write_lock[r]);

	while (writers[r])
		pthread_cond_wait(&read_cond[r], &write_lock[r]);
	readers[r]++;

	pthread_mutex_unlock(&write_lock[r]);

	// reading starts
	FILE* fptr = fopen(cmds[c][1], "r");

	fseek(fptr, 0, SEEK_END);
	int file_size = ftell(fptr);

	fclose(fptr);

	int reader_ct = 0, writer_ct = 0;
	for (int i = 0; file_name[i] != NULL; i++) {
		reader_ct += readers[i];
		writer_ct += writers[i];
	}
	printf("read %s of %d bytes with %d readers and %d writers present\n", cmds[c][1], file_size, reader_ct, writer_ct);

	pthread_mutex_lock(&write_lock[r]);
	readers[r]--;
	if (readers[r] == 0)
		pthread_cond_signal(&write_cond[r]);

	pthread_mutex_unlock(&write_lock[r]);
}

void* my_write1(void* args) {
	// printf("Hi from Writer 1!\n");

	int c = ((my_args*)args)->cmd_no,
	    w = ((my_args*)args)->write_file_no,
	    r = ((my_args*)args)->read_file_no;

	pthread_mutex_lock(&write_lock[w]);
	pthread_mutex_lock(&write_lock[r]);

	writers[w]++;

	while (active_writer[w] || readers[w])
		pthread_cond_wait(&write_cond[w], &write_lock[w]);
	active_writer[w] = true;

	while (writers[r])
		pthread_cond_wait(&read_cond[r], &write_lock[r]);

	readers[r]++;

	pthread_mutex_unlock(&write_lock[w]);
	pthread_mutex_unlock(&write_lock[r]);

	// writing starts
	FILE* fptr1 = fopen(cmds[c][2], "a");

	FILE* fptr2 = fopen(cmds[c][3], "r");
	char c_ptr = fgetc(fptr2);

	fprintf(fptr1, "\n");
	while (c_ptr != EOF) {
		fputc(c_ptr, fptr1);
		c_ptr = fgetc(fptr2);
	}

	fseek(fptr2, 0, SEEK_END);
	int file_size = ftell(fptr2);

	fclose(fptr1);
	fclose(fptr2);

	int reader_ct = 0, writer_ct = 0;
	for (int i = 0; i < FILE_CT; i++) {
		reader_ct += readers[i];
		writer_ct += writers[i];
	}
	printf("writing to %s added %d bytes with %d readers and %d writers present\n", cmds[c][2], file_size, reader_ct, writer_ct);

	pthread_mutex_lock(&write_lock[w]);
	pthread_mutex_lock(&write_lock[r]);

	writers[w]--;
	active_writer[w] = false;
	if (writers[w])
		pthread_cond_signal(&write_cond[w]);
	else
		pthread_cond_broadcast(&read_cond[w]);

	readers[r]--;
	if (readers[r] == 0)
		pthread_cond_signal(&write_cond[r]);

	pthread_mutex_unlock(&write_lock[w]);
	pthread_mutex_unlock(&write_lock[r]);
}

void* my_write2(void* args) {
	// printf("Writer 2 here!\n");
	int c = ((my_args*)args)->cmd_no,
	    w = ((my_args*)args)->write_file_no;

	pthread_mutex_lock(&write_lock[w]);
	writers[w]++;

	while (active_writer[w] || readers[w])
		pthread_cond_wait(&write_cond[w], &write_lock[w]);
	active_writer[w] = true;

	pthread_mutex_unlock(&write_lock[w]);

	// writing starts
	FILE* fptr = fopen(cmds[c][2], "a");
	fprintf(fptr, "\n%s", cmds[c][3]);
	fclose(fptr);

	int reader_ct = 0, writer_ct = 0;
	for (int i = 0; file_name[i] != NULL; i++) {
		reader_ct += readers[i];
		writer_ct += writers[i];
	}
	printf("writing to %s added %d bytes with %d readers and %d writers present\n", cmds[c][2], strlen(cmds[c][3]), reader_ct, writer_ct);

	pthread_mutex_lock(&write_lock[w]);

	writers[w]--;
	active_writer[w] = false;

	if (writers[w])
		pthread_cond_signal(&write_cond[w]);
	else
		pthread_cond_broadcast(&read_cond[w]);

	pthread_mutex_unlock(&write_lock[w]);
}

void start_read(int n) {
	my_args* args = (my_args*)malloc(sizeof(my_args*));
	args->cmd_no = n;
	for (int i = 0; file_name[i] != NULL; i++) {
		if (!strcmp(cmds[n][1], file_name[i])) {
			args->read_file_no = i;
			// printf("FOUND FILE! at %d\n", i);
			break;
		}
	}

	if (pthread_create(&th[n], NULL, &my_read, (void*)args) != 0)
		perror("Failed to create thread!");
}

void start_write2(int n) {
	my_args* args = (my_args*)malloc(sizeof(my_args*));
	args->cmd_no = n;
	for (int i = 0; file_name[i] != NULL; i++) {
		if (!strcmp(cmds[n][2], file_name[i])) {
			args->write_file_no = i;
			// printf("FOUND FILE! at %d\n", i);
			break;
		}
	}

	if (pthread_create(&th[n], NULL, &my_write2, (void*)args) != 0)
		perror("Failed to create thread!");
}

void start_write1(int n) {
	my_args* args = (my_args*)malloc(sizeof(my_args*));
	args->cmd_no = n;
	for (int i = 0; file_name[i] != NULL; i++) {
		if (!strcmp(cmds[n][2], file_name[i])) {
			args->write_file_no = i;
			// printf("FOUND WRITE FILE! at %d\n", i);
			break;
		}
	}
	for (int i = 0; file_name[i] != NULL; i++) {
		if (!strcmp(cmds[n][3], file_name[i])) {
			args->read_file_no = i;
			// printf("FOUND READ FILE! at %d\n", i);
			break;
		}
	}

	if (pthread_create(&th[n], NULL, &my_write1, (void*)args) != 0)
		perror("Failed to create thread!");
}

int main() {
	input();

	for (int i = 0; i < FILE_CT; i++) {
		pthread_mutex_init(&write_lock[i], NULL);
		pthread_cond_init(&read_cond[i], NULL);
		pthread_cond_init(&write_cond[i], NULL);
	}

	for (int i = 0; i < TOTAL_CMDS; i++) {
		if (!strcmp(cmds[i][0], "read"))
			start_read(i);
		else if (!strcmp(cmds[i][1], "1"))
			start_write1(i);
		else
			start_write2(i);
	}

	for (int i = 0; i < TOTAL_CMDS; i++)
		if (pthread_join(th[i], NULL) != 0)
			perror("Failed to join thread!");


	for (int i = 0; i < FILE_CT; i++) {
		pthread_mutex_destroy(&write_lock[i]);
		pthread_cond_destroy(&read_cond[i]);
		pthread_cond_destroy(&write_cond[i]);
	}
}

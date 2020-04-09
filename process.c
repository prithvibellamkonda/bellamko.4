#define _XOPEN_SOURCE 700
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>

#define MAX_PROCESSES 20
#define TERMINATE_PERCENTAGE 10
#define READY 0
#define BLOCKED 2

#define USED_ALL_TIME 0
#define USED_SOME_TIME 1
#define TERMINATED 3

#define check_error(expr,userMsg)\
	do {\
		if (!(expr)) {\
			perror(userMsg);\
			exit(EXIT_FAILURE);\
		}\
	} while (0)

const unsigned max_time_in_between_procs_seconds = 1;
const unsigned max_time_in_between_procs_nanoseconds = 1;

typedef struct {
    unsigned total_CPU_time_nanoseconds;
    unsigned last_burst_nanoseconds;
    unsigned simulated_pid;
    unsigned message;
    unsigned message_posted;
    unsigned time_given;
    unsigned status;
    unsigned wait_time;
    unsigned spawned;
    unsigned wait_in_queue;
} process_block;

int sig_int = 0;

void processSignal(int signum) {
	switch (signum) {
		case SIGINT:
            sig_int = 1 ;
			break;
        default:
            break;
	}
}

void* getMemoryBlock(char* fpath, unsigned size) {
	int memFd = shm_open(fpath, O_RDWR, 0600);
	check_error(memFd != -1, "...");
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	check_error(addr != MAP_FAILED, "mmap");
	close(memFd);
	return addr;
}


int main(int argc, char *argv[]) {
    check_error(argc==2, "./process proces_block");
    srand(getpid());

	process_block* self_block = getMemoryBlock(argv[1], sizeof(process_block));
    self_block->spawned = 1;

    while(1){
        signal(SIGINT,processSignal);
        if(sig_int == 1){
            check_error(munmap(self_block, sizeof(process_block)) != -1, "munmap failed");
            exit(EXIT_SUCCESS);
        }
        if((self_block->status) != READY) continue;
        if((self_block->time_given) > 0){
            if(rand()%100 < TERMINATE_PERCENTAGE){
                unsigned random_time_used = rand() % self_block->time_given + 1;
                self_block->last_burst_nanoseconds = random_time_used;
                self_block->total_CPU_time_nanoseconds += random_time_used;
                self_block->message = TERMINATED;
                self_block->message_posted = 1;
	            check_error(munmap(self_block, sizeof(process_block)) != -1, "munmap failed");
                exit(EXIT_SUCCESS);
            }
            else{
                unsigned random_time_used = rand() % self_block->time_given + 1;
                if(rand()%10 == 0) random_time_used = self_block->time_given;
                self_block->last_burst_nanoseconds = random_time_used;
                self_block->total_CPU_time_nanoseconds += random_time_used;
                unsigned time_to_wait;
                if(random_time_used != self_block->time_given){
                    time_to_wait = rand() % 1001;
                    self_block->message = USED_SOME_TIME;
                    self_block->wait_time = time_to_wait;
                    self_block->spawned = 1;
                    self_block->status = BLOCKED;
                    self_block->message_posted = 1;
                }
                else{
                    self_block->message = USED_ALL_TIME;
                    self_block->wait_time = 0;
                    self_block->spawned = 1;
                    self_block->status = BLOCKED;
                    self_block->message_posted = 1;
                }
            }
        }
    }
    return 0;
}


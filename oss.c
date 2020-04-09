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
#define READY 0
#define BLOCKED 2

#define USED_ALL_TIME 0
#define USED_SOME_TIME 1
#define TERMINATED 3

#define MAX_TIME_SINCE_LAST_PROCESS_SPAWN 50000U
#define MAX_LOG 10000U

#define check_error(expr,userMsg)\
	do {\
		if (!(expr)) {\
			perror(userMsg);\
			exit(EXIT_FAILURE);\
		}\
	} while (0)


unsigned next_pid = 0;
double wait_time = 0;
unsigned wait_n = 1;
double util_time = 0;
unsigned util_n = 1;
double idle_time = 0;
unsigned log_n = 0;
double queue_wait_time = 0;
unsigned queue_wait_n = 1;

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

typedef struct {
    unsigned total_CPU_time_nanoseconds;
    unsigned queue[4][MAX_PROCESSES];
    unsigned reserved[MAX_PROCESSES];
} oss_block;

unsigned prio_to_time(unsigned n){ //returns amount of nanoseconds based on priority
    switch(n){
        case 0:
            return 1000;
        case 1:
            return 2000;
        case 2:
            return 4000;
        case 3:
            return 8000;
        default:
            return 1000;
    }
}

void writeLog(char* msg){
    if(log_n>MAX_LOG)return; //ensures logfile doesn't contai more that 10k liknes
    FILE* log = fopen("log.log", "a");
    fprintf(log, msg);
    fclose(log);
    log_n++;
}

void writeUsedSomeTime(process_block** p_block){
    unsigned pid = (*p_block)->simulated_pid;
    unsigned time = (*p_block)->last_burst_nanoseconds;
    char msg[512];
    snprintf(msg, 512, "Process %u used %u nanoseconds\n", pid, time);
    writeLog(msg);
}

void writeMovedToQue(process_block** p_block, unsigned priority){
    unsigned pid = (*p_block)->simulated_pid;
    char msg[512];
    snprintf(msg, 512, "Process %u moved to  queue %u\n", pid, priority);
    writeLog(msg);
}

void writeUsedAllTime(process_block** p_block){
    unsigned pid = (*p_block)->simulated_pid;
    unsigned time = (*p_block)->last_burst_nanoseconds;
    char msg[512];
    snprintf(msg, 512, "Process %u used all of it's time (%u)\n", pid, time);
    writeLog(msg);
}

void writeTerminated(process_block** p_block){
    unsigned pid = (*p_block)->simulated_pid;
    unsigned time = (*p_block)->last_burst_nanoseconds;
    unsigned total = (*p_block)->total_CPU_time_nanoseconds;
    char msg[512];
    snprintf(msg, 512, "Process %u terminated after using %u nanoseconds in last burst, total time used: %u nanoseconds\n", pid, time, total);
    writeLog(msg);
}


void* createMemoryBlock(char* fpath, unsigned size) {
	int memFd = shm_open(fpath, O_RDWR|O_CREAT, 0600);
	check_error(memFd != -1, "shm_open failed");
	check_error(ftruncate(memFd, size) != -1, "ftruncate failed");
	void* addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, memFd, 0);
	check_error(addr != MAP_FAILED, "mmap failed");
	close(memFd);
	return addr;
}

void* createProcessBlock(unsigned n, unsigned size){
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    return createMemoryBlock(tmp, size);
}

void spawn_process(unsigned n){ //20 process blocks are reserved in sharem memory, new process is called by its block index
    char tmp[4];
    snprintf(tmp, 4, "/%u", n);
    pid_t pid = fork();
    if(pid == 0) check_error(execl("./process", "process", tmp, (char *) NULL) != -1, "spawning process failed");
    else return;
}

void clean_process_block(process_block** p_block){
    (*p_block)->total_CPU_time_nanoseconds = 0;
    (*p_block)->last_burst_nanoseconds = 0;
    (*p_block)->simulated_pid = next_pid; next_pid++;
    (*p_block)->message = 0;
    (*p_block)->time_given = 0;
    (*p_block)->status = BLOCKED;
    (*p_block)->wait_time = 0;
    (*p_block)->wait_in_queue = 0;
    (*p_block)->spawned = 0;
    (*p_block)->message_posted = 0;
}

void initialize(oss_block** o_block, process_block*** p_blocks){
    *p_blocks = malloc(MAX_PROCESSES * sizeof(process_block*));
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        (*p_blocks)[i] = createProcessBlock(i, sizeof(process_block));
    }
    *o_block = createMemoryBlock("/oss", sizeof(oss_block));
    (*o_block)->total_CPU_time_nanoseconds = 0;
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        (*o_block)->reserved[i] = 0;
        for(unsigned k=0; k<4; k++){
            (((*o_block)->queue)[k])[i] = 0;
        }
    }
}

void reserveBlock(oss_block** o_block, process_block*** p_blocks){ //finds first free block space in shared memory, spawns new process in there
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        if(((*o_block)->reserved)[i] == 0){
            ((*o_block)->reserved)[i] = 1;
            clean_process_block(&((*p_blocks)[i]));
            (((*o_block)->queue)[0])[i] = 1;
            writeMovedToQue(&((*p_blocks)[i]),1);
            spawn_process(i);
            return;
        }
    }
}

void give_process_time(process_block** p_block, unsigned time){
    (*p_block)->time_given = time;
    (*p_block)->status = READY;
    char msg[512];
    snprintf(msg, 512, "Process %u given %u nanoseconds\n", (*p_block)->simulated_pid, time);
    writeLog(msg);
}

void clean_on_exit(oss_block** o_block, process_block*** p_blocks){
    for(unsigned i=0; i<MAX_PROCESSES; i++){
        char tmp[4];
        snprintf(tmp, 4, "/%u", i);
	    check_error(munmap((*p_blocks)[i], sizeof(process_block)) != -1, tmp);
	    check_error(shm_unlink(tmp) != -1 , tmp);
    }
    free(*p_blocks);
	check_error(munmap(*o_block, sizeof(oss_block)) != -1, "munmap on oss block failed");
	check_error(shm_unlink("/oss") != -1 , "shm_unlink on oss block failed");
}

void prevent_aging(oss_block** o_block, process_block*** p_blocks){
    for(unsigned i = 1; i<4; i++){
        for(unsigned k = 0; k<MAX_PROCESSES; k++){
            if(((*p_blocks)[k])->wait_in_queue > prio_to_time(i)){
                (((*o_block)->queue)[i])[k] = 0;
                (((*o_block)->queue)[i-1])[k] = 1;
                writeMovedToQue(&((*p_blocks)[i]), i);
            }
        }
    }
}

void scheduler_schedule_next(oss_block** o_block, process_block*** p_blocks){
    //selects first process in highest priority queue, if there is none, moves to the second highest priority etc... 
    //selected process is given time slice, its priority is updated, log is written and blocked time is updated for all otehr processes ther are sitll blocked
    prevent_aging(o_block, p_blocks);
    for(unsigned i = 0; i<4; i++){
        for(unsigned k = 0; k<MAX_PROCESSES; k++){
            if(((*p_blocks)[k])->spawned == 1){
                if((((*o_block)->queue)[i])[k] == 1){
                    queue_wait_time += ((*p_blocks)[k])->wait_in_queue;
                    queue_wait_n++;
                    ((*p_blocks)[k])->wait_in_queue = 5;
                    for(unsigned o=0; o<MAX_PROCESSES; o++){
                        if(o == k) continue;
                        if(((*o_block)->reserved)[o] == 1){
                            ((*p_blocks)[o])->wait_in_queue += 10;
                        }
                    }
                    give_process_time(&((*p_blocks)[k]), prio_to_time(i));
                    ((*p_blocks)[k])->spawned = 0;
                    if(i<3) {
                        (((*o_block)->queue)[i])[k] = 0;
                        (((*o_block)->queue)[i+1])[k] = 1;
                        writeMovedToQue(&((*p_blocks)[i]), i+2);
                    }
                    return;
                }
            }
        }
    }
    idle_time += 5;
}

void handle_process(oss_block** o_block, process_block*** p_blocks, unsigned n){
    if(((*p_blocks)[n])->message_posted == 1){
        if(((*p_blocks)[n])->message == USED_SOME_TIME){
            (*o_block)->total_CPU_time_nanoseconds += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_time += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_n++;
            wait_time += (((*p_blocks)[n])->time_given - ((*p_blocks)[n])->last_burst_nanoseconds);
            wait_n++;
            ((*p_blocks)[n])->message_posted = 0;
            ((*p_blocks)[n])->spawned = 1;
            writeUsedSomeTime(&((*p_blocks)[n]));
        }
        else if(((*p_blocks)[n])->message == USED_ALL_TIME){
            (*o_block)->total_CPU_time_nanoseconds += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_time += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_n++;
            ((*p_blocks)[n])->message_posted = 0;
            ((*p_blocks)[n])->spawned = 1;
            writeUsedAllTime(&((*p_blocks)[n]));
        }
        else if(((*p_blocks)[n])->message == TERMINATED){
            (*o_block)->total_CPU_time_nanoseconds += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_time += ((*p_blocks)[n])->last_burst_nanoseconds;
            util_n++;
            writeTerminated(&((*p_blocks)[n]));
            clean_process_block(&((*p_blocks)[n]));
            (*o_block)->reserved[n]=0;
        }
        else{
            idle_time += 5;
        }
    }
}

void scheduler(oss_block** o_block, process_block*** p_blocks){
    scheduler_schedule_next(o_block, p_blocks);
    for(unsigned i = 0; i<MAX_PROCESSES; i++){
        handle_process(o_block, p_blocks, i);
    }
    (*o_block)->total_CPU_time_nanoseconds += 10;
}


int main() {
    system("rm -f log.log");
    oss_block* o_block; process_block** p_blocks;
    initialize(&o_block, &p_blocks);

    unsigned time_passed_from_last_proccess_spawn = 0;
    unsigned start_time = time(NULL);
    while(1){
        if((next_pid > 100) || ((time(NULL) - start_time) > 3)){ // termination criteria
            double avg_wait_time = wait_time/wait_n;
            double avg_util_time = util_time/util_n;
            double avg_queue_time = queue_wait_time/queue_wait_n;
            double total = o_block->total_CPU_time_nanoseconds;
            double util_prec = (util_time*100.0)/total;
            double idle_prec = (idle_time*100.0)/total;
            printf("Average wait time: %.2f nanoseconds\nAverage CPU utilization: %.2f nanoseconds(%.2f%%)\nAverage blocked in queue time: %.2f nanoseconds\nCPU was idle for %u naoseconds(%.2f%%)\n", avg_wait_time, avg_util_time, util_prec, avg_queue_time, (unsigned)idle_time, idle_prec);
            clean_on_exit(&o_block, &p_blocks);
            check_error(killpg(getpid(), SIGINT) != -1, "killing failed");
            while (wait(NULL) > 0);
            exit(EXIT_SUCCESS);
        }
        if(time_passed_from_last_proccess_spawn > (rand() % MAX_TIME_SINCE_LAST_PROCESS_SPAWN)){
            reserveBlock(&o_block, &p_blocks);
            time_passed_from_last_proccess_spawn = 0;
        }

        scheduler(&o_block, &p_blocks);
        time_passed_from_last_proccess_spawn += 10;
    }

    return 0;
}

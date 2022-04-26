#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* CITS2002 Project 1 2019
   Name(s):             (, Jaimin Kerai-2)
   Student number(s):   (, )
 */


//  besttq (v1.0)
//  Written by Chris.McDonald@uwa.edu.au, 2019, free for all to copy and modify

//  Compile with:  cc -std=c99 -Wall -Werror -o besttq besttq.c


//  THESE CONSTANTS DEFINE THE MAXIMUM SIZE OF TRACEFILE CONTENTS (AND HENCE
//  JOB-MIX) THAT YOUR PROGRAM NEEDS TO SUPPORT.  YOU'LL REQUIRE THESE
//  CONSTANTS WHEN DEFINING THE MAXIMUM SIZES OF ANY REQUIRED DATA STRUCTURES.

#define MAX_DEVICES             4
#define MAX_DEVICE_NAME         20
#define MAX_PROCESSES           50
// DO NOT USE THIS - #define MAX_PROCESS_EVENTS      1000
#define MAX_EVENTS_PER_PROCESS	100

#define TIME_CONTEXT_SWITCH     5
#define TIME_ACQUIRE_BUS        5


//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

int optimal_time_quantum                = 0;
int total_process_completion_time       = 0;

//OUR DEVICES
int nodevices = 0;
char devices		[MAX_DEVICES][MAX_DEVICE_NAME];
int device_rate		[MAX_DEVICES];

//OUR PROCESSES
int noprocesses = 0;
int process		[MAX_PROCESSES];
int process_start_time	[MAX_PROCESSES];

//THE EVENTS IN EACH FILE
int eventno = 0;
int event_times		[MAX_PROCESSES][MAX_EVENTS_PER_PROCESS];
char event_device	[MAX_PROCESSES][MAX_EVENTS_PER_PROCESS][MAX_DEVICE_NAME];
int event_data		[MAX_PROCESSES][MAX_EVENTS_PER_PROCESS];
int exit_time		[MAX_PROCESSES];
int process_noevents	[MAX_PROCESSES];

//JOB MIX
int RQ			[MAX_PROCESSES];
int RQ_size = 0;
int TQ_passed = 0;

int process_time_passed	[MAX_PROCESSES];

int current_time_quantum = 0;
int current_total_completion_time = 0;

int first_loop = 1;

int current_event_index	[MAX_PROCESSES];
int BQ			[MAX_PROCESSES][MAX_PROCESSES];
int BQ_size		[MAX_PROCESSES];
int databus_release_time	[MAX_PROCESSES];
int blocked_time_passed		[MAX_PROCESSES];

//  ----------------------------------------------------------------------

#define CHAR_COMMENT            '#'
#define MAXWORD                 20

void parse_tracefile(char program[], char tracefile[])
{
//  ATTEMPT TO OPEN OUR TRACEFILE, REPORTING AN ERROR IF WE CAN'T
    FILE *fp    = fopen(tracefile, "r");

    if(fp == NULL) {
        printf("%s: unable to open '%s'\n", program, tracefile);
        exit(EXIT_FAILURE);
    }

    char line[BUFSIZ];
    int  lc     = 0;

//  READ EACH LINE FROM THE TRACEFILE, UNTIL WE REACH THE END-OF-FILE
    while(fgets(line, sizeof line, fp) != NULL) {
        ++lc;

//  COMMENT LINES ARE SIMPLY SKIPPED
        if(line[0] == CHAR_COMMENT) {
            continue;
        }

//  ATTEMPT TO BREAK EACH LINE INTO A NUMBER OF WORDS, USING sscanf()
        char    word0[MAXWORD], word1[MAXWORD], word2[MAXWORD], word3[MAXWORD];
        int nwords = sscanf(line, "%s %s %s %s", word0, word1, word2, word3);

//      printf("%i = %s", nwords, line);

//  WE WILL SIMPLY IGNORE ANY LINE WITHOUT ANY WORDS
        if(nwords <= 0) {
            continue;
        }
//  LOOK FOR LINES DEFINING DEVICES, PROCESSES, AND PROCESS EVENTS
        if(nwords == 4 && strcmp(word0, "device") == 0) {
            strcpy(devices[nodevices], word1);
	    device_rate[nodevices] = atoi(word2);
	    nodevices++;
	    // FOUND A DEVICE DEFINITION, WE'LL NEED TO STORE THIS SOMEWHERE
        }

        else if(nwords == 1 && strcmp(word0, "reboot") == 0) {
            ;   // NOTHING REALLY REQUIRED, DEVICE DEFINITIONS HAVE FINISHED
        }

        else if(nwords == 4 && strcmp(word0, "process") == 0) {
            process[noprocesses] = atoi(word1);
	    process_start_time[noprocesses] = atoi(word2);
	    // FOUND THE START OF A PROCESS'S EVENTS, STORE THIS SOMEWHERE
        }

        else if(nwords == 4 && strcmp(word0, "i/o") == 0) {
	     event_times[noprocesses][eventno] = atoi(word1);
	     strcpy(event_device[noprocesses][eventno], word2);
	     event_data[noprocesses][eventno] = atoi(word3);
	     eventno++;
	    //  AN I/O EVENT FOR THE CURRENT PROCESS, STORE THIS SOMEWHERE
        }

        else if(nwords == 2 && strcmp(word0, "exit") == 0) {
            exit_time[noprocesses] = atoi(word1);
	    process_noevents[noprocesses] = eventno;
	    //  PRESUMABLY THE LAST EVENT WE'LL SEE FOR THE CURRENT PROCESS
        }

        else if(nwords == 1 && strcmp(word0, "}") == 0) {
            noprocesses++;
	    eventno = 0;
	    //  JUST THE END OF THE CURRENT PROCESS'S EVENTS
        }

        else {
            printf("%s: line %i of '%s' is unrecognized",
                        program, lc, tracefile);
            exit(EXIT_FAILURE);
        }
    }
    fclose(fp);
}

#undef  MAXWORD
#undef  CHAR_COMMENT

//  ----------------------------------------------------------------------

void append_to_queue(int process_num, int size)
{
	RQ[size] = process_num;
	RQ_size++;
}

void remove_from_queue(int size)
{
	for(int i = 0; i<size-1; i++) {
		RQ[i] = RQ[i+1];
	}
	RQ[size-1] = 0;
	RQ_size--;
}

int RQ_find_process_index(int running_process) 
{
	for (int i = 0; i<noprocesses; i++) {
		if (process[i] == running_process) {
			return i;
		}
	}
	printf("Cannot find process '%i' \n", running_process);
	exit (EXIT_FAILURE);
}

void append_to_BQ(int device_priority, int process_num, int size) 
{
	BQ[device_priority][size] = process_num;
	BQ_size[device_priority]++;
}

void remove_from_BQ(int device_priority, int size, int process_index) 
{
	for (int i = process_index; i<size-1; i++) {
		BQ[device_priority][i] = BQ[device_priority][i+1];
	}
	BQ[device_priority][size-1] = 0;
	BQ_size[device_priority]--;
}

int find_device_priority(char event_device[]) 
{
	int device_index = -1;
	int priority = 0;
	for (int i = 0; i < nodevices; i++) {
		if (strcmp(event_device, devices[i]) == 0 ) {
			device_index = i;
		}
	}

	for (int i = 0; i < nodevices; i++) {
		if (device_rate[device_index] == device_rate[i] && device_index < i) {
			priority++;
		}
		if (device_rate[device_index] < device_rate[i]) {
			priority++;
		}
	}

	return priority;
}

int find_device_speed(char event_device[]) 
{
	int device_index = -1;
	for (int i=0; i< nodevices; i++) {
		if (strcmp(event_device, devices[i]) == 0) {
			device_index = i;
		}
	}
	return device_rate[device_index];
}

int calculate_databus_time(float device_speed, float data_to_transfer) {
	float x;
	x = (data_to_transfer/device_speed)*1000000;
	float return_value = ceil(x);
	return return_value;
}

int find_process_index (int process_name) {
	int index = -1;
	for (int i = 0; i<noprocesses; i++) {
		if (process_name == process[i]) {
			return index = i;
		}
	}
	return -1;
}

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, FOR THE GIVEN TIME-QUANTUM
void simulate_job_mix(int time_quantum)
{
    printf("running simulate_job_mix( time_quantum = %i usecs )\n",
                time_quantum);

   

    int CLOCK = 0;
    int running = -1;
    int process_count = 0;
    memset(RQ, 0, MAX_PROCESSES*sizeof(RQ[0]));
    int nexit = 0;
    int current_process_index = 0;
    memset(process_time_passed, 0, MAX_PROCESSES*sizeof(process_time_passed[0]));
    RQ_size = 0;
    TQ_passed = 0;
    memset(BQ, 0, sizeof(BQ[0][0])*MAX_PROCESSES*MAX_PROCESSES);
    memset(BQ_size, 0 , sizeof(BQ_size[0])*MAX_PROCESSES);
    memset(current_event_index, 0, sizeof(current_event_index[0]) * MAX_PROCESSES);
    memset(databus_release_time, 0, sizeof(databus_release_time[0])*MAX_PROCESSES);
    memset(blocked_time_passed, -1, sizeof(blocked_time_passed[0])*MAX_PROCESSES);

    while (nexit < noprocesses) {

	    //CHECK PROCESS COMMENCE TIMES | NEW -> READY
	    if (CLOCK >= process_start_time[process_count] && process_count<noprocesses) {
		    append_to_queue(process[process_count], RQ_size);
		    process_count++;
	    }

	    //RUNNING -> BLOCKED
	    if (process_time_passed[current_process_index] >= event_times[current_process_index][ current_event_index[current_process_index] ] && running != -1 &&
	      process_noevents[current_process_index]>0 && current_event_index[current_process_index] < process_noevents[current_process_index]) {

		    char device_name[MAX_DEVICE_NAME];
		    strncpy(device_name, event_device[current_process_index][current_event_index[current_process_index]], sizeof(event_device[current_process_index]
					    [current_event_index[current_process_index]])); 
		    int device_priority = find_device_priority(device_name);
		    int data_to_transfer = event_data[current_process_index][current_event_index[current_process_index]];
		    append_to_BQ(device_priority, running, BQ_size[device_priority]); 
		    databus_release_time[current_process_index] = calculate_databus_time(find_device_speed(device_name), data_to_transfer);
		    blocked_time_passed[current_process_index] = 0;
		    running = -1;
		    
		    current_event_index[current_process_index]++;

	    }

	    //RUNNING -> READY
	    if (TQ_passed == time_quantum && running != -1 && RQ_size>0) {
		    append_to_queue(running, RQ_size);
		    running = -1;
		    
	    }

	    //BLOCKED -> READY
	    int first = 1;
	    for (int i = 0; i<MAX_PROCESSES; i++) {
		    for (int j = 0; j < BQ_size[i]; j++) {
			    int process_index = find_process_index(BQ[i][j]);
			    if (first == 1 && j==0 && databus_release_time[process_index] <= blocked_time_passed[process_index] && databus_release_time[process_index] != 0) {
				    append_to_queue(BQ[i][j], RQ_size);
				    first = 0;
				    remove_from_BQ(i, BQ_size[i], j);
				    databus_release_time[process_index] = 0;
				    blocked_time_passed[process_index] = -1;
				    CLOCK+= TIME_ACQUIRE_BUS;
				 
				    
			    }	    
		    }
	    }
	    int second = 1; 

	    for (int i = 0; i<MAX_PROCESSES; i++) {
		    for (int j = 0; j < BQ_size[i]; j++) {
			    int process_index = find_process_index(BQ[i][j]);
			    if (second == 1 && j == 0) {
				    blocked_time_passed[process_index]++;
			    }
		    }
	    }


	    //RUNNING -> RUNNING
	    if (TQ_passed == time_quantum && running != -1 && RQ_size == 0) {
		    TQ_passed = 0;
	    }


	    //READY -> RUNNING
	    if (RQ_size>0 && running==-1) {
		    CLOCK += TIME_CONTEXT_SWITCH;
		    running = RQ[0];
		    remove_from_queue(RQ_size);
		    current_process_index = RQ_find_process_index(running);
		    TQ_passed = 0;
		   
	    }

	    //RECORD PROCESS TIME PASSED
	    if (running != -1) {
		    process_time_passed[current_process_index]++;
		    TQ_passed++;
	    }

	    //CHECK WHEN TO STOP PROCESS | RUNNING -> EXIT
	    if (running != -1 && process_time_passed[current_process_index] == exit_time[current_process_index]) {
		    running = -1;
		    nexit++;
		    TQ_passed = 0;
		    

	    }
	    
	    CLOCK++;
	   // printf("%i\n", CLOCK);
    }

    
    current_time_quantum = time_quantum;
    current_total_completion_time = (CLOCK) - (process_start_time[0]);
}

//  ----------------------------------------------------------------------

void usage(char program[])
{
    printf("Usage: %s tracefile TQ-first [TQ-final TQ-increment]\n", program);
    exit(EXIT_FAILURE);
}

int main(int argcount, char *argvalue[])
{
    int TQ0 = 0, TQfinal = 0, TQinc = 0;

//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND THREE TIME VALUES
    if(argcount == 5) {
        TQ0     = atoi(argvalue[2]);
        TQfinal = atoi(argvalue[3]);
        TQinc   = atoi(argvalue[4]);

        if(TQ0 < 1 || TQfinal < TQ0 || TQinc < 1) {
            usage(argvalue[0]);
        }
    }
//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND ONE TIME VALUE
    else if(argcount == 3) {
        TQ0     = atoi(argvalue[2]);
        if(TQ0 < 1) {
            usage(argvalue[0]);
        }
        TQfinal = TQ0;
        TQinc   = 1;
    }
//  CALLED INCORRECTLY, REPORT THE ERROR AND TERMINATE
    else {
        usage(argvalue[0]);
    }

//  READ THE JOB-MIX FROM THE TRACEFILE, STORING INFORMATION IN DATA-STRUCTURES
    parse_tracefile(argvalue[0], argvalue[1]);

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, VARYING THE TIME-QUANTUM EACH TIME.
//  WE NEED TO FIND THE BEST (SHORTEST) TOTAL-PROCESS-COMPLETION-TIME
//  ACROSS EACH OF THE TIME-QUANTA BEING CONSIDERED

    for(int time_quantum=TQ0 ; time_quantum<=TQfinal ; time_quantum += TQinc) {
        simulate_job_mix(time_quantum);
	if (first_loop == 1) {
		total_process_completion_time = current_total_completion_time;
		first_loop *= -1;
	}
	if (current_total_completion_time<=total_process_completion_time) {
		optimal_time_quantum = current_time_quantum;
		total_process_completion_time = current_total_completion_time;
	}
    }

//  PRINT THE PROGRAM'S RESULT
    printf("best %i %i\n", optimal_time_quantum, total_process_completion_time);

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4

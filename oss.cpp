#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/msg.h>
#include <fstream>
#include <chrono>

#define BUFF_SZ sizeof(int) * 10
const int SHMKEY1 = 6666662;
const int SHMKEY2 = 3333331;
const int PERMS = 0644;
using namespace std;

int sharedMemId1;
int sharedMemId2;
int *nanoSecondsPtr;
int *secondsPtr;

struct ProcessControlBlock {
    int isOccupied;
    pid_t processId;
    int startSeconds;
    int startNanoSeconds;
};

struct MessageBuffer {
    long messageType;
    int seconds;
    int nanoSeconds;
};

// Handler for the SIGPROF signal (timer expiration)
void timerHandler(int dummy) {
    shmctl(sharedMemId1, IPC_RMID, NULL);
    shmctl(sharedMemId2, IPC_RMID, NULL);
    cout << "OSS has been running for 60 seconds" << endl;
    cout << "Shared memory detached" << endl;
    kill(0, SIGKILL); // Terminate all processes
    exit(1);
}

// Handler for the SIGINT signal (Ctrl-C)
void interruptHandler(int dummy) {
    shmctl(sharedMemId1, IPC_RMID, NULL);
    shmctl(sharedMemId2, IPC_RMID, NULL);
    cout << "Ctrl-C detected!" << endl;
    cout << "Shared memory detached" << endl;
    exit(1);
}

// Initializes or detaches the shared memory for the system clock
void initializeClock(int check) {
    if (check == 1) { // Initialize shared memory
        sharedMemId1 = shmget(SHMKEY1, BUFF_SZ, 0777 | IPC_CREAT);
        if (sharedMemId1 == -1) {
            fprintf(stderr, "Error shmget \n");
            exit(1);
        }
        sharedMemId2 = shmget(SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT);
        if (sharedMemId2 == -1) {
            fprintf(stderr, "Error shmget\n");
            exit(1);
        }
        cout << "Shared memory created" << endl;
        secondsPtr = (int *)(shmat(sharedMemId1, 0, 0));
        nanoSecondsPtr = (int *)(shmat(sharedMemId2, 0, 0));
        *secondsPtr = 0;
        *nanoSecondsPtr = 0;
    } else { // Detach shared memory
        shmdt(secondsPtr);
        shmdt(nanoSecondsPtr);
        shmctl(sharedMemId1, IPC_RMID, NULL);
        shmctl(sharedMemId2, IPC_RMID, NULL);
        cout << "Shared memory detached" << endl;
    }
}

// Increments the system clock by a fixed amount
void incrementSystemClock() {
    int *clockSeconds = (int *)(shmat(sharedMemId1, 0, 0));
    int *clockNanoSeconds = (int *)(shmat(sharedMemId2, 0, 0));
    *clockNanoSeconds += 10000;
    if (*clockNanoSeconds >= 1000000000) {
        *clockNanoSeconds -= 1000000000;
        (*clockSeconds)++;
    }
    shmdt(clockSeconds);
    shmdt(clockNanoSeconds);
}

// Sets up the signal handlers for SIGINT and SIGPROF
void setupSignalHandlers() {
    signal(SIGINT, interruptHandler);
    struct sigaction act;
    act.sa_handler = timerHandler;
    act.sa_flags = 0;
    if (sigemptyset(&act.sa_mask) || sigaction(SIGPROF, &act, NULL)) {
        perror("Failed to set up handler for SIGPROF");
        exit(1);
    }
    struct itimerval value;
    value.it_interval.tv_sec = 60;
    value.it_interval.tv_usec = 0;
    value.it_value = value.it_interval;
    if (setitimer(ITIMER_PROF, &value, NULL) == -1) {
        perror("Failed to set up the ITIMER_PROF interval timer");
        exit(1);
    }
}

// Prints the process table and system clock to the console and log file
void printProcessTable(const ProcessControlBlock *procTable, ofstream &logFile, int *clockSeconds, int *clockNanoSeconds) {
    cout << "OSS PID: " << getpid() << " SysClockS: " << *clockSeconds << " SysClockNano: " << *clockNanoSeconds << endl;
    logFile << "OSS PID: " << getpid() << " SysClockS: " << *clockSeconds << " SysClockNano: " << *clockNanoSeconds << endl;
    cout << "Process Table: " << endl;
    logFile << "Process Table: " << endl;
    cout << "Entry Occupied PID   StartS StartN" << endl;
    logFile << "Entry Occupied PID   StartS StartN" << endl;
    for (int i = 0; i < 10; i++) {
        cout << i << "     " << procTable[i].isOccupied << "        " << procTable[i].processId << " " << procTable[i].startSeconds << "      " << procTable[i].startNanoSeconds << endl;
        logFile << i << "     " << procTable[i].isOccupied << "        " << procTable[i].processId << " " << procTable[i].startSeconds << "      " << procTable[i].startNanoSeconds << endl;
    }
}

int main(int argc, char *argv[]) {
    setupSignalHandlers();
    int opt;
    int nValue = 5, sValue = 2, tValue = 5, iValue = 100; // Default values for command line arguments
    string fValue; // Log file name

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "h:n:s:t:f:i:")) != -1) {
        switch (opt) {
        case 'h':
            cout << "Usage: ./oss -n proc -s simul -t time -i interval -f logfile" << endl;
            exit(1);
        case 'n':
            nValue = atoi(optarg);
            break;
        case 's':
            sValue = atoi(optarg);
            break;
        case 't':
            tValue = atoi(optarg);
            break;
        case 'f':
            fValue = optarg;
            break;
        case 'i':
            iValue = atoi(optarg);
            break;
        }
    }

    ofstream logFile(fValue.c_str()); // Open log file
    srand((unsigned)time(NULL)); // Seed for random number generation
    initializeClock(1); // Initialize shared memory for system clock

    // Set up message queue for communication with worker processes
    key_t key;
    if ((key = ftok("oss.cpp", 'B')) == -1) {
        perror("ftok");
        exit(1);
    }
    int msgQueueId;
    if ((msgQueueId = msgget(key, PERMS | IPC_CREAT)) == -1) {
        perror("msgget");
        exit(1);
    }

    MessageBuffer msgBuf; // Buffer for message queue communication
    msgBuf.messageType = 1; // Set message type
    ProcessControlBlock procTable[10] = {}; // Initialize process table
    int *clockSeconds = (int *)(shmat(sharedMemId1, 0, 0)); // Pointer to shared memory for seconds
    int *clockNanoSeconds = (int *)(shmat(sharedMemId2, 0, 0)); // Pointer to shared memory for nanoseconds
    auto lastLaunchTime = chrono::steady_clock::now(); // Timestamp for last process launch
    int tableIndex = 0; // Index for process table
    int waitCheck, halfSecCheck = 0; // Variables for process termination and half-second check

    // Main loop for launching and managing worker processes
    while (nValue > 0) {
        incrementSystemClock(); // Increment system clock
        if (*clockNanoSeconds == 500000000) { // Check for half-second
            halfSecCheck = 1;
        }
        if (halfSecCheck == 1) { // Print process table every half-second
            printProcessTable(procTable, logFile, clockSeconds, clockNanoSeconds);
            halfSecCheck = 0;
        }
        waitCheck = waitpid(-1, nullptr, WNOHANG); // Check for terminated processes
        auto currentTime = chrono::steady_clock::now(); // Get current time
        // Check if it's time to launch a new process
        if (chrono::duration_cast<chrono::milliseconds>(currentTime - lastLaunchTime).count() >= iValue && tableIndex < sValue) {
            int sec = rand() % tValue; // Random seconds for process duration
            int nano = rand() % 1000000000; // Random nanoseconds for process duration
            msgBuf.seconds = sec;
            msgBuf.nanoSeconds = nano;
            const char *args[] = {"./worker", NULL}; // Arguments for worker process
            nValue--;
            pid_t pid = fork(); // Fork a new process
            if (pid == 0) { // Child process (worker)
                if (msgsnd(msgQueueId, &msgBuf, 20, 0) == -1)
                    perror("msgsnd");
                execvp(args[0], const_cast<char* const*>(args)); // Execute worker program
                cout << "Exec failed! Terminating" << endl;
                logFile << "Exec failed! Terminating" << endl;
                exit(1);
            } else { // Parent process (OSS)
                procTable[tableIndex].isOccupied = 1;
                procTable[tableIndex].processId = pid;
                procTable[tableIndex].startSeconds = *clockSeconds;
                procTable[tableIndex].startNanoSeconds = *clockNanoSeconds;
                tableIndex++;
                lastLaunchTime = chrono::steady_clock::now(); // Update last launch time
            }
        }
        if (waitCheck > 0) { // Process has terminated
            for (int i = 0; i < 10; i++) {
                if (procTable[i].processId == waitCheck) {
                    procTable[i].isOccupied = 0; // Mark process table entry as vacant
                }
            }
        }
    }

    // Wait for all remaining processes to terminate
    int occupiedCheck = 0;
    for (int i = 0; i < 10; i++) {
        if (procTable[i].isOccupied == 1) {
            occupiedCheck++;
        }
    }
    while (occupiedCheck > 0) {
        incrementSystemClock(); // Increment system clock
        if (*clockNanoSeconds == 500000000) { // Check for half-second
            halfSecCheck = 1;
        }
        if (halfSecCheck == 1) { // Print process table every half-second
            printProcessTable(procTable, logFile, clockSeconds, clockNanoSeconds);
            halfSecCheck = 0;
        }
        waitCheck = waitpid(-1, nullptr, WNOHANG); // Check for terminated processes
        if (waitCheck > 0) { // Process has terminated
            for (int i = 0; i < 10; i++) {
                if (procTable[i].processId == waitCheck) {
                    procTable[i].isOccupied = 0; // Mark process table entry as vacant
                    occupiedCheck--;
                }
            }
        }
    }

    // Final process table print and cleanup
    printProcessTable(procTable, logFile, clockSeconds, clockNanoSeconds);
    cout << "Oss finished" << endl;
    logFile << "Oss finished" << endl;
    shmdt(clockSeconds); // Detach shared memory for seconds
    shmdt(clockNanoSeconds); // Detach shared memory for nanoseconds
    if (msgctl(msgQueueId, IPC_RMID, NULL) == -1) { // Remove message queue
        perror("msgctl");
        exit(1);
    }
    initializeClock(0); // Detach and remove shared memory for system clock
    return 0;
}

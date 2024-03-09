#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>

#define BUFF_SZ sizeof ( int ) * 10
const int SHMKEY1 = 6666662;
const int SHMKEY2 = 3333331;
const int PERMS = 0644;

//message buffer
struct msgbuffer {
   long mtype; //needed
   int sec; //stores time to kill sec
   int nano; //stores time to kill nano sec
};

using namespace std;

int main(int argc, char *argv[])  {

//receive msg
  struct msgbuffer buf;
  int msqid;
  key_t key;
  int sec, nano;

  if ((key = ftok("oss.cpp", 'B')) == -1) {
      perror("ftok");
      exit(1);
   }

   if ((msqid = msgget(key, PERMS)) == -1) { /* connect to the queue */
      perror("msgget");
      exit(1);
   }
   //receive message
   if (msgrcv(msqid, &buf, 20, 0, 0) == -1) {
       perror("msgrcv");
       exit(1);
   }
   sec = buf.sec;
   nano = buf.nano;


//shared memory attaching
  int shmid1 = shmget ( SHMKEY1, BUFF_SZ, 0777);
  if ( shmid1 == -1 ) {
      fprintf(stderr,"Error in shmget ...\n");
      exit (1);
  }
  int shmid2 = shmget ( SHMKEY2, BUFF_SZ, 0777);
  if ( shmid2 == -1 ) {
      fprintf(stderr,"Error in shmget ...\n");
      exit (1);
  }
  int * clockSec = ( int * )( shmat ( shmid1, 0, 0 ) );
  int * clockNano = ( int * )( shmat ( shmid2, 0, 0 ) );
  int startTime = *clockSec;

  int timeCheck = 0;



  cout << "Worker PID: " << getpid() << " PPID: " << getppid() << " SysClockS: " << *clockSec << " SysClockNano: " << *clockNano << " TermTimeS: " << sec << " TermTimeNano: " << nano <<    " --Just starting" << endl;
  while (*clockSec <= sec) {
      if(startTime < *clockSec) {
        startTime++;
        timeCheck++;
        cout << "Worker PID: " << getpid() << " PPID: " << getppid() << " SysClockS: " << *clockSec << " SysClockNano: " << *clockNano << " TermTimeS: " << sec << " TermTimeNano: " << nano <<    " --" << timeCheck << " seconds have passed since starting" << endl;
      }
  }

  shmdt(clockSec);
  shmdt(clockNano);

  return 0;

}

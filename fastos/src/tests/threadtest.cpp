// Copyright 2017 Yahoo Holdings. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "tests.h"
#include "job.h"
#include "thread_test_base.hpp"
#include <vespa/fastos/time.h>
#include <cstdlib>

#define MUTEX_TEST_THREADS 6
#define MAX_THREADS 7


class ThreadTest : public ThreadTestBase
{
   int Main () override;

   void WaitForXThreadsToHaveWait (Job *jobs,
                                   int jobCount,
                                   FastOS_Cond *condition,
                                   int numWait)
   {
      Progress(true, "Waiting for %d threads to be in wait state", numWait);

      int oldNumber=-10000;
      for(;;)
      {
         int waitingThreads=0;

         condition->Lock();

         for(int i=0; i<jobCount; i++)
         {
            if(jobs[i].result == 1)
               waitingThreads++;
         }

         condition->Unlock();

         if(waitingThreads != oldNumber)
            Progress(true, "%d threads are waiting", waitingThreads);

         oldNumber = waitingThreads;

         if(waitingThreads == numWait)
            break;

         FastOS_Thread::Sleep(100);
      }
   }

   void TooManyThreadsTest ()
   {
      TestHeader("Too Many Threads Test");

      FastOS_ThreadPool *pool = new FastOS_ThreadPool(128*1024, MAX_THREADS);

      if(Progress(pool != NULL, "Allocating ThreadPool"))
      {
         int i;
         Job jobs[MAX_THREADS];

         for(i=0; i<MAX_THREADS; i++)
         {
            jobs[i].code = PRINT_MESSAGE_AND_WAIT3SEC;
            jobs[i].message = static_cast<char *>(malloc(100));
            sprintf(jobs[i].message, "Thread %d invocation", i+1);
         }

         for(i=0; i<MAX_THREADS+1; i++)
         {
            if(i==MAX_THREADS)
            {
               bool rc = (NULL == pool->NewThread(this,
                                  static_cast<void *>(&jobs[0])));
               Progress(rc, "Creating too many threads should fail.");
            }
            else
            {
               bool rc = (NULL != pool->NewThread(this,
                                  static_cast<void *>(&jobs[i])));
               Progress(rc, "Creating Thread");
            }
         };

         WaitForThreadsToFinish(jobs, MAX_THREADS);

         Progress(true, "Verifying result codes...");
         for(i=0; i<MAX_THREADS; i++)
         {
            Progress(jobs[i].result ==
                     static_cast<int>(strlen(jobs[i].message)),
                     "Checking result code from thread (%d==%d)",
                     jobs[i].result, strlen(jobs[i].message));
         }

         Progress(true, "Closing threadpool...");
         pool->Close();

         Progress(true, "Deleting threadpool...");
         delete(pool);
      }
      PrintSeparator();
   }


   void HowManyThreadsTest ()
   {
      #define HOW_MAX_THREADS (1024)
      TestHeader("How Many Threads Test");

      FastOS_ThreadPool *pool = new FastOS_ThreadPool(128*1024, HOW_MAX_THREADS);

      if(Progress(pool != NULL, "Allocating ThreadPool"))
      {
         int i;
         Job jobs[HOW_MAX_THREADS];

         for(i=0; i<HOW_MAX_THREADS; i++)
         {
            jobs[i].code = PRINT_MESSAGE_AND_WAIT3SEC;
            jobs[i].message = static_cast<char *>(malloc(100));
            sprintf(jobs[i].message, "Thread %d invocation", i+1);
         }

         for(i=0; i<HOW_MAX_THREADS; i++)
         {
            if(i==HOW_MAX_THREADS)
            {
               bool rc = (NULL == pool->NewThread(this,
                                  static_cast<void *>(&jobs[0])));
               Progress(rc, "Creating too many threads should fail.");
            }
            else
            {
               bool rc = (NULL != pool->NewThread(this,
                                  static_cast<void *>(&jobs[i])));
               Progress(rc, "Creating Thread");
            }
         };

         WaitForThreadsToFinish(jobs, HOW_MAX_THREADS);

         Progress(true, "Verifying result codes...");
         for(i=0; i<HOW_MAX_THREADS; i++)
         {
            Progress(jobs[i].result ==
                     static_cast<int>(strlen(jobs[i].message)),
                     "Checking result code from thread (%d==%d)",
                     jobs[i].result, strlen(jobs[i].message));
         }

         Progress(true, "Closing threadpool...");
         pool->Close();

         Progress(true, "Deleting threadpool...");
         delete(pool);
      }
      PrintSeparator();
   }

   void CreateSingleThreadAndJoin ()
   {
      TestHeader("Create Single Thread And Join Test");

      FastOS_ThreadPool *pool = new FastOS_ThreadPool(128*1024);

      if(Progress(pool != NULL, "Allocating ThreadPool"))
      {
         Job job;

         job.code = NOP;
         job.result = -1;

         bool rc = (NULL != pool->NewThread(this, &job));
         Progress(rc, "Creating Thread");

         WaitForThreadsToFinish(&job, 1);
      }

      Progress(true, "Closing threadpool...");
      pool->Close();

      Progress(true, "Deleting threadpool...");
      delete(pool);
      PrintSeparator();
   }

  void ThreadCreatePerformance (bool silent, int count, int outercount)
  {
    int i;
    int j;
    bool rc;
    int threadsok;
    FastOS_Time starttime;
    FastOS_Time endtime;
    FastOS_Time usedtime;

    if (!silent)
      TestHeader("Thread Create Performance");

    FastOS_ThreadPool *pool = new FastOS_ThreadPool(128 * 1024);

    if (!silent)
      Progress(pool != NULL, "Allocating ThreadPool");
    if (pool != NULL) {
      Job *jobs = new Job[count];

      threadsok = 0;
      starttime.SetNow();
      for (i = 0; i < count; i++) {
        jobs[i].code = SILENTNOP;
        jobs[i].ownThread = pool->NewThread(this, &jobs[i]);
        rc = (jobs[i].ownThread != NULL);
        if (rc)
          threadsok++;
      }

      for (j = 0; j < outercount; j++) {
        for (i = 0; i < count; i++) {
          if (jobs[i].ownThread != NULL)
            jobs[i].ownThread->Join();
          jobs[i].ownThread = pool->NewThread(this, &jobs[i]);
          rc = (jobs[i].ownThread != NULL);
          if (rc)
            threadsok++;
        }
      }
      for (i = 0; i < count; i++) {
        if (jobs[i].ownThread != NULL)
          jobs[i].ownThread->Join();
      }
      endtime.SetNow();
      usedtime = endtime;
      usedtime -= starttime;

      if (!silent) {
        Progress(true, "Used time: %d.%03d",
                 usedtime.GetSeconds(), usedtime.GetMicroSeconds() / 1000);

        double timeused = usedtime.GetSeconds() +
          static_cast<double>(usedtime.GetMicroSeconds()) / 1000000.0;
        ProgressFloat(true, "Threads/s: %6.1f",
                      static_cast<float>(static_cast<double>(threadsok) /
                              timeused));
      }
      if (threadsok != ((outercount + 1) * count))
        Progress(false, "Only started %d of %d threads", threadsok,
                 (outercount + 1) * count);

      if (!silent)
        Progress(true, "Closing threadpool...");
      pool->Close();
      delete [] jobs;

      if (!silent)
        Progress(true, "Deleting threadpool...");
      delete(pool);
      if (!silent)
        PrintSeparator();
    }
  }

  void ClosePoolStability(void) {
    int i;
    TestHeader("ThreadPool close stability test");
    for (i = 0; i < 8000; i++) {
      // Progress(true, "Creating pool iteration %d", i + 1);
      ThreadCreatePerformance(true, 2, 1);
    }
    PrintSeparator();
  }



   void ClosePoolTest ()
   {
      TestHeader("Close Pool Test");

      FastOS_ThreadPool pool(128*1024);
      const int closePoolThreads=9;
      Job jobs[closePoolThreads];

      number = 0;

      for(int i=0; i<closePoolThreads; i++)
      {
         jobs[i].code = INCREASE_NUMBER;

         bool rc = (NULL != pool.NewThread(this,
                            static_cast<void *>(&jobs[i])));
         Progress(rc, "Creating Thread %d", i+1);
      }

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");
      PrintSeparator();
   }

   void BreakFlagTest ()
   {
      TestHeader("BreakFlag Test");

      FastOS_ThreadPool pool(128*1024);

      const int breakFlagThreads=4;

      Job jobs[breakFlagThreads];

      for(int i=0; i<breakFlagThreads; i++)
      {
         jobs[i].code = WAIT_FOR_BREAK_FLAG;

         bool rc = (NULL != pool.NewThread(this,
                            static_cast<void *>(&jobs[i])));
         Progress(rc, "Creating Thread %d", i+1);
      }

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");
      PrintSeparator();
   }

   void SharedSignalAndBroadcastTest (Job *jobs, int numThreads,
                                      FastOS_Cond *condition,
                                      FastOS_ThreadPool *pool)
   {
      for(int i=0; i<numThreads; i++)
      {
         jobs[i].code = WAIT_FOR_CONDITION;
         jobs[i].condition = condition;
         jobs[i].ownThread = pool->NewThread(this,
                 static_cast<void *>(&jobs[i]));

         bool rc=(jobs[i].ownThread != NULL);
         Progress(rc, "CreatingThread %d", i+1);
      }

      WaitForXThreadsToHaveWait (jobs, numThreads,
                                 condition, numThreads);

      // Threads are not guaranteed to have entered sleep yet,
      // as this test only tests for result code
      // Wait another second to be sure.
      FastOS_Thread::Sleep(1000);
   }

   void SignalTest ()
   {
      const int numThreads = 5;

      TestHeader("Signal Test");

      FastOS_ThreadPool pool(128*1024);
      Job jobs[numThreads];
      FastOS_Cond condition;

      SharedSignalAndBroadcastTest(jobs, numThreads, &condition, &pool);

      for(int i=0; i<numThreads; i++)
      {
         condition.Signal();
         WaitForXThreadsToHaveWait(jobs, numThreads,
                                   &condition, numThreads-1-i);
      }

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");
      PrintSeparator();
   }

   void BroadcastTest ()
   {
      TestHeader("Broadcast Test");

      const int numThreads = 5;

      FastOS_ThreadPool pool(128*1024);
      Job jobs[numThreads];
      FastOS_Cond condition;

      SharedSignalAndBroadcastTest(jobs, numThreads, &condition, &pool);

      condition.Broadcast();
      WaitForXThreadsToHaveWait(jobs, numThreads, &condition, 0);

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");
      PrintSeparator();
   }

   void ThreadIdTest ()
   {
      const int numThreads = 5;
      int i,j;

      TestHeader ("Thread Id Test");

      FastOS_ThreadPool pool(128*1024);
      Job jobs[numThreads];
      FastOS_Mutex slowStartMutex;

      slowStartMutex.Lock();    // Halt all threads until we want them to run

      for(i=0; i<numThreads; i++) {
         jobs[i].code = TEST_ID;
         jobs[i].result = -1;
         jobs[i]._threadId = 0;
         jobs[i].mutex = &slowStartMutex;
         jobs[i].ownThread = pool.NewThread(this,
                 static_cast<void *>(&jobs[i]));
         bool rc=(jobs[i].ownThread != NULL);
         if(rc)
            jobs[i]._threadId = jobs[i].ownThread->GetThreadId();
         Progress(rc, "CreatingThread %d id:%lu", i+1,
                  (unsigned long)(jobs[i]._threadId));

         for(j=0; j<i; j++) {
            if(jobs[j]._threadId == jobs[i]._threadId) {
               Progress(false,
                        "Two different threads received the "
                        "same thread id (%lu)",
                        (unsigned long)(jobs[i]._threadId));
            }
         }
      }

      slowStartMutex.Unlock();    // Allow threads to run

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");

      for(i=0; i<numThreads; i++) {
         Progress(jobs[i].result == 1,
                  "Thread %lu: ID comparison (current vs stored)",
                  (unsigned long)(jobs[i]._threadId));
      }

      PrintSeparator();
   }

   void TimedWaitTest ()
   {
      TestHeader("Cond Timed Wait Test");

      FastOS_ThreadPool pool(128*1024);
      Job job;
      FastOS_Cond condition;

      job.code = WAIT2SEC_AND_SIGNALCOND;
      job.result = -1;
      job.condition = &condition;
      job.ownThread = pool.NewThread(this,
                                     static_cast<void *>(&job));

      Progress(job.ownThread !=NULL, "Creating thread");

      if(job.ownThread != NULL)
      {
         condition.Lock();
         bool gotCond = condition.TimedWait(500);
         Progress(!gotCond, "We should not get the condition just yet (%s)",
                  gotCond ? "got it" : "didn't get it");
         gotCond = condition.TimedWait(500);
         Progress(!gotCond, "We should not get the condition just yet (%s)",
                  gotCond ? "got it" : "didn't get it");
         gotCond = condition.TimedWait(5000);
         Progress(gotCond, "We should have got the condition now (%s)",
                  gotCond ? "got it" : "didn't get it");
         condition.Unlock();
      }

      Progress(true, "Waiting for threads to finish using pool.Close()...");
      pool.Close();
      Progress(true, "Pool closed.");

      PrintSeparator();
   }

   void LeakTest ()
   {
      TestHeader("Leak Test");

      int allocCount = 2 * 1024 * 1024;
      int progressIndex= allocCount/8;
      int i;

      for(i=0; i<allocCount; i++)
      {
         FastOS_Mutex *mtx = new FastOS_Mutex();
         mtx->Lock();
         mtx->Unlock();
         delete mtx;

         if((i % progressIndex) == (progressIndex - 1))
            Progress(true, "Tested %d FastOS_Mutex instances", i + 1);
      }

      for(i=0; i<allocCount; i++)
      {
         FastOS_Cond *cond = new FastOS_Cond();
         delete cond;

         if((i % progressIndex) == (progressIndex - 1))
            Progress(true, "Tested %d FastOS_Cond instances", i+1);
      }

      for(i=0; i<allocCount; i++)
      {
         FastOS_BoolCond *cond = new FastOS_BoolCond();
         delete cond;

         if((i % progressIndex) == (progressIndex - 1))
            Progress(true, "Tested %d FastOS_BoolCond instances", i+1);
      }

      PrintSeparator();
   }

   void SynchronizationStressTest ()
   {
      TestHeader("Synchronization Object Stress Test");

      const int allocCount = 150000;
      int i;

      FastOS_Mutex **mutexes = new FastOS_Mutex*[allocCount];

      FastOS_Time startTime, nowTime;
      startTime.SetNow();

      for(i=0; i<allocCount; i++)
         mutexes[i] = new FastOS_Mutex();

      nowTime.SetNow();
      Progress(true, "Allocated %d mutexes at time: %d ms", allocCount,
               static_cast<int>(nowTime.MilliSecs() - startTime.MilliSecs()));

      for(int e=0; e<4; e++)
      {
         for(i=0; i<allocCount; i++)
            mutexes[i]->Lock();

         for(i=0; i<allocCount; i++)
            mutexes[i]->Unlock();

         nowTime.SetNow();
         Progress(true, "Tested %d mutexes at time: %d ms", allocCount,
                  static_cast<int>(nowTime.MilliSecs() -
                                   startTime.MilliSecs()));
      }
      for(i=0; i<allocCount; i++)
         delete mutexes[i];

      nowTime.SetNow();
      Progress(true, "Deleted %d mutexes at time: %d ms", allocCount,
               static_cast<int>(nowTime.MilliSecs() - startTime.MilliSecs()));

      delete [] mutexes;

      PrintSeparator();
   }

};

int ThreadTest::Main ()
{
   printf("grep for the string '%s' to detect failures.\n\n", failString);
   time_t before = time(0);

   //   HowManyThreadsTest();
   SynchronizationStressTest();
   LeakTest();
   TimedWaitTest();
   ThreadIdTest();
   SignalTest();
   BroadcastTest();
   CreateSingleThreadAndJoin();
   TooManyThreadsTest();
   ClosePoolTest();
   BreakFlagTest();
   CreateSingleThreadAndJoin();
   BroadcastTest();
   SignalTest();
   ThreadCreatePerformance(false, 500, 100);
   ClosePoolStability();
   { time_t now = time(0); printf("[%ld seconds]\n", now-before); before = now; }

   printf("END OF TEST (%s)\n", _argv[0]);
   return allWasOk() ? 0 : 1;
}

int main (int argc, char **argv)
{
   ThreadTest app;
   setvbuf(stdout, NULL, _IOLBF, 8192);
   return app.Entry(argc, argv);
}

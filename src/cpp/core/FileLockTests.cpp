/*
 * FileLocktests.cpp
 *
 * Copyright (C) 2009-16 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#ifndef _WIN32

#include <core/FileLock.hpp>

#include <sys/wait.h>

#include <shared_core/Error.hpp>
#include <shared_core/FilePath.hpp>

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#define RSTUDIO_NO_TESTTHAT_ALIASES
#include <tests/TestThat.hpp>

namespace rstudio {
namespace core {
namespace tests {

int s_lockCount = 0;
boost::thread_group s_threads;
boost::mutex s_mutex;
FilePath s_lockFilePath("/tmp/rstudio-test-lock");
   
void acquireLock(std::size_t threadNumber)
{
   LinkBasedFileLock lock;
   Error error = lock.acquire(s_lockFilePath);
   if (!error)
   {
      s_mutex.lock();
      ++s_lockCount;
      s_mutex.unlock();
   }
}

void forkAndCheckLock()
{
   // fork process
   pid_t child = ::fork();
   if (child == 0)
   {
      // the child process should see that the file is locked
      AdvisoryFileLock childLock;
      CHECK(childLock.isLocked(s_lockFilePath));

      // if locked, the child process should not be able to acquire the lock
      Error error = childLock.acquire(s_lockFilePath);
      CHECK_FALSE(!error);

      // the lock should remain after this attempt to lock
      CHECK(childLock.isLocked(s_lockFilePath));

      // exit successfully
      ::exit(EXIT_SUCCESS);
   }
   
   // parent waits for child process to exit
   int status;
   ::waitpid(child, &status, 0);
}
   
TEST_CASE("File Locking", "[!hide]")
{
   SECTION("A link-based lock can only be acquired once")
   {
      Error error;
      
      LinkBasedFileLock lock1;
      LinkBasedFileLock lock2;
      
      error = lock1.acquire(s_lockFilePath);
      if (error)
         LOG_ERROR(error);
      
      CHECK(!error);
      
      error = lock2.acquire(s_lockFilePath);
      if (error)
         LOG_ERROR(error);
      
      CHECK_FALSE(!error);
      
      // release and re-acquire
      error = lock1.release();
      if (error)
         LOG_ERROR(error);
      
      CHECK(!error);
      
      error = lock2.acquire(s_lockFilePath);
      if (error)
         LOG_ERROR(error);
      
      CHECK(!error);
      
      // clean up the lockfile when we're done
      s_lockFilePath.removeIfExists();
   }
   
   SECTION("Only one thread successfully acquires link-based file lock")
   {
      // this test can potentially take a long time due to the large amount of threads
      // there have been cases where the default timeout (30 sec) would cause the test to fail
      // as later threads would see the lockfile as stale
      // increase the timeout for this test and restore it afterwards
      boost::posix_time::seconds timeout = FileLock::getTimeoutInterval();
      FileLock::setTimeoutInterval(boost::posix_time::seconds(600)); // 10 minutes

      for (std::size_t i = 0; i < 100; ++i)
         s_threads.create_thread(boost::bind(acquireLock, i));
      
      s_threads.join_all();
      CHECK(s_lockCount == 1);

      // clean up
      s_lockFilePath.removeIfExists();
      FileLock::setTimeoutInterval(timeout);
   }

   SECTION("child processes generated by ::fork() don't clear registry")
   {
      LinkBasedFileLock lock;
      Error error = lock.acquire(s_lockFilePath);
      if (error)
         LOG_ERROR(error);
      
      CHECK(s_lockFilePath.exists());
      CHECK(lock.isLocked(s_lockFilePath));
      
      pid_t child = ::fork();
      if (child == 0)
      {
         // tell the child process to exit (running any destructors and so on)
         ::exit(0);
      }
      else
      {
         // wait for child process to exit
         int status;
         ::waitpid(child, &status, 0);
         
         // ensure that the child process didn't take our locks down with it
         CHECK(s_lockFilePath.exists());
         CHECK(lock.isLocked(s_lockFilePath));
      }
      
      // clean up
      s_lockFilePath.removeIfExists();
   }
   
   SECTION("basic advisory file lock assumptions hold true")
   {
      Error error;
      
      // create lock file
      error = s_lockFilePath.ensureFile();
      CHECK(!error);
      
      // ensure file is not locked
      AdvisoryFileLock lock;
      CHECK(s_lockFilePath.exists());
      CHECK_FALSE(lock.isLocked(s_lockFilePath));
      
      // attempt to acquire that lock
      error = lock.acquire(s_lockFilePath);
      CHECK(!error);
      
      // check lock in child process
      forkAndCheckLock();

      // ensure lockfile still exists
      CHECK(s_lockFilePath.exists());
      
      // check whether other locks kill existing locks
      {
         AdvisoryFileLock transientLock;
         error = transientLock.acquire(s_lockFilePath);
         CHECK(!error);
         forkAndCheckLock();
      }
      
      // TODO: this fails (the destructor above kills lock)
      // forkAndCheckLock();
      
      // ensure lock acquired
      error = lock.acquire(s_lockFilePath);
      CHECK(!error);
      
      // TODO: checking if a file is locked from same process will release lock
      // lock.isLocked(s_lockFilePath);
      // forkAndCheckLock();

      // clean up lockfile
      s_lockFilePath.removeIfExists();
   }
}

} // end namespace tests
} // end namespace core
} // end namespace rstudio

#endif // _WIN32

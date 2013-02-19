
// DO NOT EDIT THIS FILE - it is machine generated -*- c++ -*-

#ifndef __java_util_concurrent_ExecutorCompletionService__
#define __java_util_concurrent_ExecutorCompletionService__

#pragma interface

#include <java/lang/Object.h>

class java::util::concurrent::ExecutorCompletionService : public ::java::lang::Object
{

  ::java::util::concurrent::RunnableFuture * newTaskFor(::java::util::concurrent::Callable *);
  ::java::util::concurrent::RunnableFuture * newTaskFor(::java::lang::Runnable *, ::java::lang::Object *);
public:
  ExecutorCompletionService(::java::util::concurrent::Executor *);
  ExecutorCompletionService(::java::util::concurrent::Executor *, ::java::util::concurrent::BlockingQueue *);
  virtual ::java::util::concurrent::Future * submit(::java::util::concurrent::Callable *);
  virtual ::java::util::concurrent::Future * submit(::java::lang::Runnable *, ::java::lang::Object *);
  virtual ::java::util::concurrent::Future * take();
  virtual ::java::util::concurrent::Future * poll();
  virtual ::java::util::concurrent::Future * poll(jlong, ::java::util::concurrent::TimeUnit *);
public: // actually package-private
  static ::java::util::concurrent::BlockingQueue * access$0(::java::util::concurrent::ExecutorCompletionService *);
private:
  ::java::util::concurrent::Executor * __attribute__((aligned(__alignof__( ::java::lang::Object)))) executor;
  ::java::util::concurrent::AbstractExecutorService * aes;
  ::java::util::concurrent::BlockingQueue * completionQueue;
public:
  static ::java::lang::Class class$;
};

#endif // __java_util_concurrent_ExecutorCompletionService__

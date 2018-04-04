#include <stdio.h>
#include <future>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

/*
* Thread task interface.
*/
class IThreadTask {
public:

  /*
   * Default constructor.
   */
  IThreadTask (void) = default;

  /*
   * Run the task.
   */
  virtual void execute() = 0;

  /*
   * Default moving operation.
   */
  IThreadTask(IThreadTask&& other) = default;
  IThreadTask& operator=(IThreadTask&& other) = default;

  /*
   * Not copyable.
   */
  IThreadTask(const IThreadTask& rhs) = delete;
  IThreadTask& operator=(const IThreadTask& rhs) = delete;

  /*
   * Default deconstructor.
   */
  virtual ~IThreadTask(void) = default;
};

/*
* An implementation of the thread task interface.
*/
template <typename Func>
class ThreadTask: public IThreadTask {
public:

  /*
   * Constructor.
   */
  ThreadTask (Func&& func);

  /*
   * Default moving operation.
   */
  ThreadTask (ThreadTask&& other) = default;
  ThreadTask& operator= (ThreadTask&& other) = default;

  /*
   * Not copyable.
   */
  ThreadTask (const ThreadTask& rhs) = delete;
  ThreadTask& operator= (const ThreadTask& rhs) = delete;

  /*
   * Default deconstructor.
   */
  ~ThreadTask(void) override = default;

  /*
   * Run the task.
   */
  void execute() override ;

private:
  Func m_func;
};

template <typename Func>
ThreadTask<Func>::ThreadTask (Func&& func)
  :m_func{std::move(func)}
  {
  return ;
}

template <typename Func>
void ThreadTask<Func>::execute (void){
  this->m_func();

  return ;
}

/*
* A wrapper around a std::future that adds the behavior of futures returned from std::async.
* Specifically, this object will block and wait for execution to finish before going out of scope.
*/
template <typename T>
class TaskFuture {
public:
  TaskFuture(std::future<T>&& future)
    :m_future{std::move(future)}
    {
    return ;
  }

  TaskFuture(const TaskFuture& rhs) = delete;
  TaskFuture& operator=(const TaskFuture& rhs) = delete;
  TaskFuture(TaskFuture&& other) = default;
  TaskFuture& operator=(TaskFuture&& other) = default;

  ~TaskFuture(void) {

    /*
     * Check if we have a result to wait.
     */
    if(!m_future.valid()) {
      return ;
    }

    /*
     * Wait for the result.
     */
    m_future.get();

    return ;
  }

  auto get(void) {
    return m_future.get();
  }

private:
  std::future<T> m_future;
};

template <typename T>
class ThreadSafeQueue{
public:

  /*
   * Attempt to get the first value in the queue.
   * Returns true if a value was successfully written to the out parameter, false otherwise.
   */
  bool tryPop (T& out);

  /*
   * Get the first value in the queue.
   * Will block until a value is available unless clear is called or the instance is destructed.
   * Returns true if a value was successfully written to the out parameter, false otherwise.
   */
  bool waitPop (T& out);
  bool waitPop (void);

  /*
   * Push a new value onto the queue.
   */
  void push (T value);

  /*
   * Push a new value onto the queue if the queue size is less than maxSize.
   * Otherwise, wait for it to happen and then push the new value.
   */
  bool waitPush (T value, int64_t maxSize);

  /*
   * Clear all items from the queue.
   */
  void clear (void);

  /*
   * Invalidate the queue.
   * Used to ensure no conditions are being waited on in waitPop when
   * a thread or the application is trying to exit.
   * The queue is invalid after calling this method and it is an error
   * to continue using a queue after this method has been called.
   */
  void invalidate(void);

  /*
   * Check whether or not the queue is empty.
   */
  bool empty (void) const;

  /*
   * Return the number of elements in the queue.
   */
  int64_t size (void) const;

  /*
   * Returns whether or not this queue is valid.
   */
  bool isValid(void) const;
  
  /*
   * Destructor.
   */
  ~ThreadSafeQueue(void);

  /*
   * Default constructor.
   */
  ThreadSafeQueue (void) = default;

  /*
   * Not copyable.
   */
  ThreadSafeQueue (const ThreadSafeQueue & other) = delete;
  ThreadSafeQueue & operator= (const ThreadSafeQueue & other) = delete;

  /*
   * Not assignable.
   */
  ThreadSafeQueue (const ThreadSafeQueue && other) = delete;
  ThreadSafeQueue & operator= (const ThreadSafeQueue && other) = delete;

private:
  std::atomic_bool m_valid{true};
  mutable std::mutex m_mutex;
  std::queue<T> m_queue;
  std::condition_variable empty_condition;
  std::condition_variable full_condition;

  void internal_push (T& value);
  void internal_pop (T& out);
};

template <typename T>
bool ThreadSafeQueue<T>::tryPop (T& out){
  std::lock_guard<std::mutex> lock{m_mutex};
  if(m_queue.empty() || !m_valid){
    return false;
  }

  internal_pop(out);

  return true;
}

template <typename T>
bool ThreadSafeQueue<T>::waitPop (T& out){
  std::unique_lock<std::mutex> lock{m_mutex};

  /*
   * Check if the queue is not valid anymore.
   */
  if(!m_valid) {
    return false;
  }

  /*
   * We need to wait until the queue is not empty.
   *
   * Check if the queue is empty.
   */
  if (m_queue.empty()){

    /*
     * Wait until the queue will be in a valid state and it will be not empty.
     */
    empty_condition.wait(lock, 
      [this]()
      {
        return !m_queue.empty() || !m_valid;
      }
    );
  }

  /*
   * Using the condition in the predicate ensures that spurious wakeups with a valid
   * but empty queue will not proceed, so only need to check for validity before proceeding.
   */
  if(!m_valid) {
    return false;
  }

  internal_pop(out);

  return true;
}

template <typename T>
bool ThreadSafeQueue<T>::waitPop (void){
  std::unique_lock<std::mutex> lock{m_mutex};

  /*
   * Check if the queue is not valid anymore.
   */
  if(!m_valid) {
    return false;
  }

  /*
   * We need to wait until the queue is not empty.
   *
   * Check if the queue is empty.
   */
  if (m_queue.empty()){

    /*
     * Wait until the queue will be in a valid state and it will be not empty.
     */
    empty_condition.wait(lock, 
      [this]()
      {
        return !m_queue.empty() || !m_valid;
      }
    );
  }

  /*
   * Using the condition in the predicate ensures that spurious wakeups with a valid
   * but empty queue will not proceed, so only need to check for validity before proceeding.
   */
  if(!m_valid) {
    return false;
  }

  /*
   * Pop the top element from the queue.
   */
  this->m_queue.pop();

  /*
   * Notify about the fact that the queue might be not full now.
   */
  this->full_condition.notify_one();

  return true;
}

template <typename T>
void ThreadSafeQueue<T>::push (T value){
  std::lock_guard<std::mutex> lock{m_mutex};
  internal_push(value);

  return ;
}
 
template <typename T>
bool ThreadSafeQueue<T>::waitPush (T value, int64_t maxSize){
  std::unique_lock<std::mutex> lock{m_mutex};

  full_condition.wait(lock, 
    [this, maxSize]()
    {
      return (m_queue.size() < maxSize) || !m_valid;
    }
  );

  /*
   * Using the condition in the predicate ensures that spurious wakeups with a valid
   * but empty queue will not proceed, so only need to check for validity before proceeding.
   */
  if(!m_valid) {
    return false;
  }

  internal_push(value);

  return true;
}

template <typename T>
bool ThreadSafeQueue<T>::empty (void) const {
  std::lock_guard<std::mutex> lock{m_mutex};

  return m_queue.empty();
}

template <typename T>
int64_t ThreadSafeQueue<T>::size (void) const {
  std::lock_guard<std::mutex> lock{m_mutex};

  return m_queue.size();
}

template <typename T>
void ThreadSafeQueue<T>::clear (void) {
  std::lock_guard<std::mutex> lock{m_mutex};
  while(!m_queue.empty()) {
    m_queue.pop();
  }
  full_condition.notify_all();

  return ;
}

template <typename T>
void ThreadSafeQueue<T>::invalidate (void) {
  std::lock_guard<std::mutex> lock{m_mutex};

  /*
   * Check if the queue has been already invalidated.
   */
  if (!m_valid){
    return ;
  }

  /*
   * Invalidate the queue.
   */
  m_valid = false;
  empty_condition.notify_all();
  full_condition.notify_all();

  return ;
}

template <typename T>
bool ThreadSafeQueue<T>::isValid (void) const {
  std::lock_guard<std::mutex> lock{m_mutex};
    
  return m_valid;
}

template <typename T>
ThreadSafeQueue<T>::~ThreadSafeQueue(void){
  this->invalidate();

  return ;
}

template <typename T>
void ThreadSafeQueue<T>::internal_push (T& value){

  /*
   * Push the value to the queue.
   */
  m_queue.push(std::move(value));

  /*
   * Notify that the queue is not empty.
   */
  empty_condition.notify_one();

  return ;
}

template <typename T>
void ThreadSafeQueue<T>::internal_pop (T& out){

  /*
   * Fetch the element on top of the queue.
   */
  out = std::move(m_queue.front());

  /*
   * Pop the top element from the queue.
   */
  this->m_queue.pop();

  /*
   * Notify about the fact that the queue might be not full now.
   */
  full_condition.notify_one();

  return ;
}

/*
* Thread pool.
*/
class ThreadPool {
public:

  /*
   * Default constructor.
   */
  ThreadPool(void)
    :ThreadPool{std::max(std::thread::hardware_concurrency(), 2u) - 1u}
  {
    /*
     * Always create at least one thread.  If hardware_concurrency() returns 0,
     * subtracting one would turn it to UINT_MAX, so get the maximum of
     * hardware_concurrency() and 2 before subtracting 1.
     */
  }

  /*
   * Constructor.
   */
  explicit ThreadPool (const std::uint32_t numThreads, std::function <void (void)> codeToExecuteAtDeconstructor = nullptr)
    :
    m_done{false},
    m_workQueue{},
    m_threads{}
  {
    try {

      /*
       * Initialize the per-thread flags.
       */
      this->threadAvailability = new std::atomic_bool[numThreads];
      for(auto i = 0u; i < numThreads; ++i) {
        this->threadAvailability[i] = true;
      }

      /*
       * Start threads.
       */
      for(auto i = 0u; i < numThreads; ++i) {
        m_threads.emplace_back(&ThreadPool::worker, this, &(this->threadAvailability[i]));
      }

    } catch(...) {
      destroy();
      throw;
    }

    if (codeToExecuteAtDeconstructor != nullptr){
      this->codeToExecuteByTheDeconstructor.push(codeToExecuteAtDeconstructor);
    }
  }

  void appendCodeToDeconstructor (std::function<void ()> codeToExecuteAtDeconstructor) {
    this->codeToExecuteByTheDeconstructor.push(codeToExecuteAtDeconstructor);

    return ;
  }

  /*
   * Submit a job to be run by the thread pool.
   */
  template <typename Func, typename... Args>
  auto submit (Func&& func, Args&&... args) {

    /*
     * Making the task.
     */
    auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    using ResultType = std::result_of_t<decltype(boundTask)()>;
    using PackagedTask = std::packaged_task<ResultType()>;
    using TaskType = ThreadTask<PackagedTask>;
    PackagedTask task{std::move(boundTask)};

    /*
     * Create the future.
     */
    TaskFuture<ResultType> result{task.get_future()};
    
    /*
     * Submit the task.
     */
    m_workQueue.push(std::make_unique<TaskType>(std::move(task)));
  
    return result;
  }

  /*
   * Submit a job to be run by the thread pool and detach it from the caller.
   */
  template <typename Func, typename... Args>
  void submitAndDetach (Func&& func, Args&&... args) {

    /*
     * Making the task.
     */
    auto boundTask = std::bind(std::forward<Func>(func), std::forward<Args>(args)...);
    using ResultType = std::result_of_t<decltype(boundTask)()>;
    using PackagedTask = std::packaged_task<ResultType()>;
    using TaskType = ThreadTask<PackagedTask>;
    PackagedTask task{std::move(boundTask)};

    /*
     * Submit the task.
     */
    m_workQueue.push(std::make_unique<TaskType>(std::move(task)));

    return ;
  }

  std::uint32_t numberOfIdleThreads (void) const {
    std::uint32_t n = 0;

    for (auto i=0; i < this->m_threads.size(); i++){
      if (this->threadAvailability[i]){
        n++;
      }
    }

    return n;
  }

  std::uint64_t numberOfTasksWaitingToBeProcessed (void) const {
    return this->m_workQueue.size();
  }

  /*
   * Destructor.
   */
  ~ThreadPool(void) {
    destroy();

    return ;
  }

  /*
   * Non-copyable.
   */
  ThreadPool(const ThreadPool& rhs) = delete;

  /*
   * Non-assignable.
   */
  ThreadPool& operator=(const ThreadPool& rhs) = delete;

private:

  /*
   * Constantly running function each thread uses to acquire work items from the queue.
   */
  void worker (std::atomic_bool *availability){
    while(!m_done) {
      *availability = true;
      std::unique_ptr<IThreadTask> pTask{nullptr};
      if(m_workQueue.waitPop(pTask)) {
        *availability = false;
        pTask->execute();
      }
    }

    return ;
  }

  /*
   * Invalidates the queue and joins all running threads.
   */
  void destroy(void) {

    /*
     * Execute the user code.
     */
    while (codeToExecuteByTheDeconstructor.size() > 0){
      std::function<void ()> code;
      codeToExecuteByTheDeconstructor.waitPop(code);
      code();
    }

    /*
     * Signal threads to quite.
     */
    m_done = true;
    m_workQueue.invalidate();

    /*
     * Join threads.
     */
    for(auto& thread : m_threads) {
      if(!thread.joinable()) {
        continue ;
      }
      thread.join();
    }
    delete[] this->threadAvailability;

    return ;
  }

  /*
   * Object fields.
   */
  std::atomic_bool m_done;
  ThreadSafeQueue<std::unique_ptr<IThreadTask>> m_workQueue;
  std::vector<std::thread> m_threads;
  std::atomic_bool *threadAvailability;
  ThreadSafeQueue<std::function<void ()>> codeToExecuteByTheDeconstructor;
};

extern "C" void printReachedIter(int iter){
  printf("Iter:\t%d\n", iter);
}

extern "C" void queuePush(ThreadSafeQueue<int> *queue, int val){
  //printf("Pushing val:%d\n", val);
  queue->push(val);
  //printf("Pushed val:%d\n", val);
}

extern "C" void queuePop(ThreadSafeQueue<int> *queue, int &val){
  //printf("Popping val\n");
  while (!queue->waitPop(val))
    printf("Spurious pop\n");
  //printf("Popped val:%d\n", val);
}

extern "C" void stageExecuter(void (*stage)(void *, void *), void *env, void *queues){ return stage(env, queues); }

extern "C" void stageDispatcher(void *env, void *queues, void *stages, int numberOfStages, int numberOfQueues){
  ThreadSafeQueue<int> *localQueues[numberOfQueues];
  for (int i = 0; i < numberOfQueues; ++i)
  {
    localQueues[i] = new ThreadSafeQueue<int>();
  }
  queues = localQueues;

  ThreadPool pool(numberOfStages);
  auto localFutures = (TaskFuture<void> *)malloc(numberOfStages * sizeof(TaskFuture<void>));
  for (int i = 0; i < numberOfStages; ++i)
  {
    auto stage = ((void (**)(void *, void *)) stages)[i];
    localFutures[i] = std::move(pool.submit(stage, env, queues));
  }

  for (int i = 0; i < numberOfStages; ++i)
  {
    localFutures[i].get();
  }

  for (int i = 0; i < numberOfQueues; ++i)
  {
    delete localQueues[i];
  }
}

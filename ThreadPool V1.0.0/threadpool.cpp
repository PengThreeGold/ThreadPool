#include "threadpool.h"
#include <iostream>
#include <functional>
#include <thread>


const int TASK_MAX_THRESHHOLD = 1024;  // 最大任务数
const int ThREAD_MAX_THRESHHOLD = 100; // 最大线程数
const int THREAD_MAX_IDLE_TIME = 60;   // 默认：60s


/*****************************************************************************/
/************************     ThreadPool方法实现    **************************/
/*****************************************************************************/


// 线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	, taskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(ThREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// 线程池析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// notEmpty_.notify_all();

	// 等待线程池里面所有的线程返回
	// 线程池线程状态：1、阻塞 & 2、执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	std::cout << "ThreadPool has been exited !" << std::endl;
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

// 设置
void ThreadPool::setThreadSizeThreshHold_(int threshHold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadSizeThreshHold_ = threshHold;
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	// 设置线程池的运行状态
	isPoolRunning_ = true;

	// 记录初始线程数量
	initThreadSize_ = initThreadSize;

	// 基础当前线程数量
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		// 使用绑定器绑定类对象（ThreadPool）函数（ThreadFunc），传入Thread构造函数中
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// 启动线程对象
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // 需要执行线程函数
		idleThreadSize_++; // 记录初始空闲线程的数量
	}
}

// 给线程池提交任务， 用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 线程的通信 等待任务队列空余信号
	/*
	while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}
	notFull_.wait(lock, [&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; });
	wait 阻塞线程直到被唤醒
	wait_for 阻塞线程直到被唤醒或者阻塞时长超过指定时长
	wait_untill 阻塞线程直到被唤醒或者阻塞时间超过指定时间点
	*/

	// 用户提交任务最长不能超过1s，否则判断提交任务失败，进行返回
	if (notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() >= (size_t)taskQueMaxThreshHold_; }))
	{
		// 表示notFull_等待1s，条件依然没有满足
		std::cerr << "Task queue is full, submit task fail." << std::endl;
		return Result(sp, false); // 错误信息
	}


	// 如果有空余则进行提交
	taskQue_.emplace(sp);
	taskSize_++;

	// 新放入的任务，任务队列不为空，在 notEmpty_ 上进行通知，赶快分配线程执行任务
	notEmpty_.notify_all();

	// Cached模式：任务处理比较紧急
	// 场景：小而快的任务
	// 根据任务数量和空闲线程的数量，判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> Create new thread." << std::endl;

		// 创建新线程
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start();
		// 修改线程个数相关的变量
		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的Result对象
	// 方式1、return task->getResult(); // 线程执行完Task对象就被析构掉了，之后所有依赖于Task对象的操作将无效
	// 方式2、Result(task);
	return Result(sp);
}

// 定义线程函数，线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadId)
{
	// 当前起始时间记录
	auto lastTime = std::chrono::high_resolution_clock().now();

	// 所有任务执行完毕，线程池才可以开始回收所有线程资源

	for (;;)
	{
		std::shared_ptr<Task> task;

		// 获取任务并释放锁
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "Thread: " << std::this_thread::get_id() << " Try to get the task." << std::endl;

			// 判断任务队列是否为空
			// 为空进入while循环处理线程池
			// 不为空执行任务
			while (taskQue_.size() == 0)
			{
				// 线程池要结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadId); // std::this_thread::getid() 线程池定义的id
					std::cout << "Thread: " << std::this_thread::get_id() << " Exit!" << std::endl;
					exitCond_.notify_all();
					return; // 线程函数结束，线程结束
				}

				// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s
				// 应该把多余的线程回收掉（超过initThreadSize_数量的线程要进行回收）
				// 当前时间 - 上一次线程执行的时间 > 60s
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 条件变量超时返回
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							// 开始回收当前线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象从线程列表容器中删除
							// 考虑：threadFunc -> thread对象 ？
							// threadid -> thread对象 -> 删除
							threads_.erase(threadId);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "Thread: " << std::this_thread::get_id() << " Exit!" << std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notEmpty条件
					// 当所有的任务都被取出后，而且不再增加任务
					// 此处会一直等待，没有通知，发生死锁
					notEmpty_.wait(lock);
				}

			}

			// 取出线程工作
			idleThreadSize_--;

			std::cout << "Thread: " << std::this_thread::get_id() << " For mission success." << std::endl;

			// 从任务队列中取出任务
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，通知其它线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 取出一个任务后，需要进行通知，通知可以继续提交生产任务
			notFull_.notify_all();
		}

		// 当前线程负责执行此任务
		if (task != nullptr)
		{
			// task->run();
			// 功能需求执行任务、把任务的返回值通过getVal方法给到Result

			task->exec();
		}

		// 线程工作完毕
		idleThreadSize_++;
		// 更新线程执行完任务的时间
		lastTime = std::chrono::high_resolution_clock().now();
	}
}


/*****************************************************************************/
/***************************    Task方法实现    ******************************/
/*****************************************************************************/


// 构造函数
Task::Task()
	:result_(nullptr)
{}

// 设置Task类内Result对象指针
void Task::setResult(Result* res)
{
	result_ = res;
}

// 执行函数
void Task::exec()
{
	if (result_ != nullptr)
	{
		// Task对象内存储的Result对象指针指向（调用）获取线程run函数返回值的函数
		result_->setVal(run()); // run函数根据任务不同（继承基类）会发生多态（派生类）
	}
}


/*****************************************************************************/
/**************************    Thread方法实现    *****************************/
/*****************************************************************************/


// 初始化静态变量
int Thread::generateId_ = 0;

// 构造函数
Thread::Thread(THreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

// 析构函数
Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
	// 创建一个线程来执行一个线程函数
	std::thread t(func_, threadId_); // C++11来说 线程对象t 线程函数func_
	t.detach(); // 设置分离线程 Linux下的pthread_detach
}

// 获取线程id
int Thread::getId() const
{
	return threadId_;
}


/*****************************************************************************/
/***************************    Result方法实现    ****************************/
/*****************************************************************************/


// 构造函数
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)

{
	task_->setResult(this);
}

// 获取线程执行完毕后的返回值
void Result::setVal(Any any)
{
	// 存储task的返回值
	this->any_ = std::move(any);

	// 已经获取的任务的返回值，增加信号量资源
	sem_.post();
}

// 获取任务返回值
Any Result::get()
{
	// 判断提交任务是否可用
	if (!isValid_)
	{
		return "";
	}

	// task任务如果没有执行完，这里会阻塞用户的线程
	sem_.wait();

	return std::move(any_);
}

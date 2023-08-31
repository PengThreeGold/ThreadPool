#include "threadpool.h"
#include <iostream>
#include <functional>
#include <thread>


const int TASK_MAX_THRESHHOLD = 1024;  // ���������
const int ThREAD_MAX_THRESHHOLD = 100; // ����߳���
const int THREAD_MAX_IDLE_TIME = 60;   // Ĭ�ϣ�60s


/*****************************************************************************/
/************************     ThreadPool����ʵ��    **************************/
/*****************************************************************************/


// �̳߳ع���
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

// �̳߳�����
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// notEmpty_.notify_all();

	// �ȴ��̳߳��������е��̷߳���
	// �̳߳��߳�״̬��1������ & 2��ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
	std::cout << "ThreadPool has been exited !" << std::endl;
}

// ����̳߳ص�����״̬
bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

// �����̳߳صĹ���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// ����task�������������ֵ
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

// �����̳߳�cachedģʽ���߳���ֵ
void ThreadPool::setThreadSizeThreshHold_(int threshHold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
		threadSizeThreshHold_ = threshHold;
}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// �����̳߳ص�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�߳�����
	initThreadSize_ = initThreadSize;

	// ������ǰ�߳�����
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		// ʹ�ð����������ThreadPool��������ThreadFunc��������Thread���캯����
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// �����̶߳���
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫִ���̺߳���
		idleThreadSize_++; // ��¼��ʼ�����̵߳�����
	}
}

// ���̳߳��ύ���� �û����øýӿڣ��������������������
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// �̵߳�ͨ�� �ȴ�������п����ź�
	/*
	while (taskQue_.size() == taskQueMaxThreshHold_)
	{
		notFull_.wait(lock);
	}
	notFull_.wait(lock, [&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; });
	wait �����߳�ֱ��������
	wait_for �����߳�ֱ�������ѻ�������ʱ������ָ��ʱ��
	wait_untill �����߳�ֱ�������ѻ�������ʱ�䳬��ָ��ʱ���
	*/

	// �û��ύ��������ܳ���1s�������ж��ύ����ʧ�ܣ����з���
	if (notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() >= (size_t)taskQueMaxThreshHold_; }))
	{
		// ��ʾnotFull_�ȴ�1s��������Ȼû������
		std::cerr << "Task queue is full, submit task fail." << std::endl;
		return Result(sp, false); // ������Ϣ
	}


	// ����п���������ύ
	taskQue_.emplace(sp);
	taskSize_++;

	// �·��������������в�Ϊ�գ��� notEmpty_ �Ͻ���֪ͨ���Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	// Cachedģʽ��������ȽϽ���
	// ������С���������
	// �������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> Create new thread." << std::endl;

		// �������߳�
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		// threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// �����߳�
		threads_[threadId]->start();
		// �޸��̸߳�����صı���
		curThreadSize_++;
		idleThreadSize_++;
	}

	// ���������Result����
	// ��ʽ1��return task->getResult(); // �߳�ִ����Task����ͱ��������ˣ�֮������������Task����Ĳ�������Ч
	// ��ʽ2��Result(task);
	return Result(sp);
}

// �����̺߳������̳߳ص������̴߳��������������������
void ThreadPool::threadFunc(int threadId)
{
	// ��ǰ��ʼʱ���¼
	auto lastTime = std::chrono::high_resolution_clock().now();

	// ��������ִ����ϣ��̳߳زſ��Կ�ʼ���������߳���Դ

	for (;;)
	{
		std::shared_ptr<Task> task;

		// ��ȡ�����ͷ���
		{
			// �Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "Thread: " << std::this_thread::get_id() << " Try to get the task." << std::endl;

			// �ж���������Ƿ�Ϊ��
			// Ϊ�ս���whileѭ�������̳߳�
			// ��Ϊ��ִ������
			while (taskQue_.size() == 0)
			{
				// �̳߳�Ҫ�����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadId); // std::this_thread::getid() �̳߳ض����id
					std::cout << "Thread: " << std::this_thread::get_id() << " Exit!" << std::endl;
					exitCond_.notify_all();
					return; // �̺߳����������߳̽���
				}

				// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s
				// Ӧ�ðѶ�����̻߳��յ�������initThreadSize_�������߳�Ҫ���л��գ�
				// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ����������ʱ����
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							// ��ʼ���յ�ǰ�߳�
							// ��¼�߳���������ر�����ֵ�޸�
							// ���̶߳�����߳��б�������ɾ��
							// ���ǣ�threadFunc -> thread���� ��
							// threadid -> thread���� -> ɾ��
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
					// �ȴ�notEmpty����
					// �����е����񶼱�ȡ���󣬶��Ҳ�����������
					// �˴���һֱ�ȴ���û��֪ͨ����������
					notEmpty_.wait(lock);
				}

			}

			// ȡ���̹߳���
			idleThreadSize_--;

			std::cout << "Thread: " << std::this_thread::get_id() << " For mission success." << std::endl;

			// �����������ȡ������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// �����Ȼ��ʣ������֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ���������Ҫ����֪ͨ��֪ͨ���Լ����ύ��������
			notFull_.notify_all();
		}

		// ��ǰ�̸߳���ִ�д�����
		if (task != nullptr)
		{
			// task->run();
			// ��������ִ�����񡢰�����ķ���ֵͨ��getVal��������Result

			task->exec();
		}

		// �̹߳������
		idleThreadSize_++;
		// �����߳�ִ���������ʱ��
		lastTime = std::chrono::high_resolution_clock().now();
	}
}


/*****************************************************************************/
/***************************    Task����ʵ��    ******************************/
/*****************************************************************************/


// ���캯��
Task::Task()
	:result_(nullptr)
{}

// ����Task����Result����ָ��
void Task::setResult(Result* res)
{
	result_ = res;
}

// ִ�к���
void Task::exec()
{
	if (result_ != nullptr)
	{
		// Task�����ڴ洢��Result����ָ��ָ�򣨵��ã���ȡ�߳�run��������ֵ�ĺ���
		result_->setVal(run()); // run������������ͬ���̳л��ࣩ�ᷢ����̬�������ࣩ
	}
}


/*****************************************************************************/
/**************************    Thread����ʵ��    *****************************/
/*****************************************************************************/


// ��ʼ����̬����
int Thread::generateId_ = 0;

// ���캯��
Thread::Thread(THreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{}

// ��������
Thread::~Thread()
{}

// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_); // C++11��˵ �̶߳���t �̺߳���func_
	t.detach(); // ���÷����߳� Linux�µ�pthread_detach
}

// ��ȡ�߳�id
int Thread::getId() const
{
	return threadId_;
}


/*****************************************************************************/
/***************************    Result����ʵ��    ****************************/
/*****************************************************************************/


// ���캯��
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)

{
	task_->setResult(this);
}

// ��ȡ�߳�ִ����Ϻ�ķ���ֵ
void Result::setVal(Any any)
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);

	// �Ѿ���ȡ������ķ���ֵ�������ź�����Դ
	sem_.post();
}

// ��ȡ���񷵻�ֵ
Any Result::get()
{
	// �ж��ύ�����Ƿ����
	if (!isValid_)
	{
		return "";
	}

	// task�������û��ִ���꣬����������û����߳�
	sem_.wait();

	return std::move(any_);
}
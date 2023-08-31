#ifndef  THREADPOOL_H // if no defined 防止头文件的重复包含和编译，便于程序的调试和移植
#define THREADPOOL_H // 

#include <unordered_map>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <iostream>

// Any类型：可以接受任意数据的类型
class Any
{
public:
	// 构造函数
	Any() = default;

	// 析构函数
	~Any() = default;

	// 删除左值引用的拷贝构造
	Any(const Any&) = delete;

	// 删除左值引用的赋值运算符拷贝构造
	Any& operator=(const Any&) = delete;

	// 添加右值引用的拷贝构造
	Any(Any&&) = default;

	// 添加右值引用的赋值运算符拷贝构造
	Any& operator=(Any&&) = default;

	// 这个构造函数可以让Any类型接受任意其他的数据
	template<typename T>
	Any(T data)
		: base_(std::make_unique<Derive<T>>(data))
	{}

	// 这个方法能把Any对象里面存储的data数据提取出来
	template<typename T>
	T cast_()
	{
		// 我们怎么从base_找到它所指向的Derive对象
		// 从该对象里面取出data成员变量

		// 基类指针 =》 派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) // 类型匹配不成功
		{
			throw "type is incompatible！";
		}
		return pd->data_;
	}

private:
	// 基类类型
	class Base
	{
	public:
		virtual ~Base() = default;

		Base() = default;
	};

	// 派生类类型
	template <typename T>
	class Derive : public Base
	{
	public:
		Derive(T data)
			:data_(data)
		{}

		T data_;

		Derive() = default;
	};

private:
	std::unique_ptr<Base> base_;
};

// 实现一个信号量类
class Semaphore
{
public:
	// 构造函数
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}

	// 析构函数
	~Semaphore()
	{
		isExit_ = true;
	}

	// 获取一个信号量资源
	void wait()
	{
		if (isExit_) // 出作用域开始析构了不需要继续操作
		{
			return;
		}
		std::unique_lock<std::mutex> lock(mtx_);
		// 等待信号量有资源，没有资源的话会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// 增加一个信号量资源
	void post()
	{
		if (isExit_) // 出作用域开始析构了不需要继续操作
		{
			return;
		}
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// Linux下的condition_variable的析构函数未进行处理
		// 导致此处状态失效，无故阻塞
		cond_.notify_all(); // 等待状态，释放mutex锁，通知条件变量wait的地方继续工作
	}

private:
	std::atomic_bool isExit_;
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

// Task类型的前置声明
class Task;

// 实现接收提交到线程池的task任务执行完成后的返回值类型Result
class Result
{
public:
	// 构造函数
	Result(std::shared_ptr<Task> task, bool isValid = true);

	// 析构函数
	~Result() = default;

	// setVal方法，获取任务执行完成的返回值
	void setVal(Any any);

	// get方法，用户调用这个方法获取task的返回值
	Any get();

private:
	Any any_;  // 存储任务的返回值
	Semaphore sem_; // 线程通信信号量
	std::shared_ptr<Task> task_; // 指向对应获取返回值的任务对象
	std::atomic_bool isValid_; // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:
	// 构造函数
	Task();

	// 析构函数
	~Task() = default;

	// 获取Result对象
	void setResult(Result* res);

	// 执行函数
	void exec();

	// 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	Result* result_; // Result 对象的生命周期 > Task对象（Result所持有）的生命周期
};

// 线程类型
class Thread
{
public:
	// 线程函数对象类型
	using THreadFunc = std::function<void(int)>;

	// 线程构造
	Thread(THreadFunc func);

	// 线程析构
	~Thread();

	// 启动线程
	void start();

	// 获取线程id
	int getId() const;

private:
	THreadFunc func_;
	static int generateId_;
	int threadId_; // 保存线程id
};

// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED, // 固定数量的线程
	MODE_CACHED, // 线程数量可动态增长
};

// 线程池类型
class ThreadPool
{
public:
	// 线程池构造
	ThreadPool();

	// 线程池析构
	~ThreadPool();

	// 开启线程池
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// 设置线程池的工作模式
	void setMode(PoolMode mode);

	// 设置task任务队列上限阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold_(int threshHold);

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	// 删除拷贝构造函数
	ThreadPool(const ThreadPool&) = delete;

	// 删除赋值重载运算符
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 线程函数
	void threadFunc(int theadId);

	// 检查线程池的运行状态
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_; // 线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // 线程容器
	int initThreadSize_; // 初始的线程数量
	std::atomic_int idleThreadSize_; //空闲线程的数量
	std::atomic_int curThreadSize_; // 记录当前线程池里面线程的总数量
	int threadSizeThreshHold_;// 线程数量上限阈值

	std::queue<std::shared_ptr<Task>> taskQue_; // 任务队列
	std::atomic_int taskSize_; // 任务的数量
	int taskQueMaxThreshHold_; // 任务队列数量上限阈值

	std::mutex taskQueMtx_; // 保证任务队列的线程安全
	std::condition_variable notFull_; // 表示任务队列不满
	std::condition_variable notEmpty_; // 表示任务队列不空
	std::condition_variable exitCond_; // 等待线程资源全部回收

	PoolMode poolMode_; // 当前线程池的工作模式
	std::atomic_bool isPoolRunning_;// 表示当前线程池的启动状态
};

#endif //  THREADPOOL_H

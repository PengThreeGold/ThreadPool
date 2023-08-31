#ifndef  THREADPOOL_H // if no defined ��ֹͷ�ļ����ظ������ͱ��룬���ڳ���ĵ��Ժ���ֲ
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

// Any���ͣ����Խ����������ݵ�����
class Any
{
public:
	// ���캯��
	Any() = default;

	// ��������
	~Any() = default;

	// ɾ����ֵ���õĿ�������
	Any(const Any&) = delete;

	// ɾ����ֵ���õĸ�ֵ�������������
	Any& operator=(const Any&) = delete;

	// �����ֵ���õĿ�������
	Any(Any&&) = default;

	// �����ֵ���õĸ�ֵ�������������
	Any& operator=(Any&&) = default;

	// ������캯��������Any���ͽ�����������������
	template<typename T>
	Any(T data)
		: base_(std::make_unique<Derive<T>>(data))
	{}

	// ��������ܰ�Any��������洢��data������ȡ����
	template<typename T>
	T cast_()
	{
		// ������ô��base_�ҵ�����ָ���Derive����
		// �Ӹö�������ȡ��data��Ա����

		// ����ָ�� =�� ������ָ��
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) // ����ƥ�䲻�ɹ�
		{
			throw "type is incompatible��";
		}
		return pd->data_;
	}

private:
	// ��������
	class Base
	{
	public:
		virtual ~Base() = default;

		Base() = default;
	};

	// ����������
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

// ʵ��һ���ź�����
class Semaphore
{
public:
	// ���캯��
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}

	// ��������
	~Semaphore()
	{
		isExit_ = true;
	}

	// ��ȡһ���ź�����Դ
	void wait()
	{
		if (isExit_) // ��������ʼ�����˲���Ҫ��������
		{
			return;
		}
		std::unique_lock<std::mutex> lock(mtx_);
		// �ȴ��ź�������Դ��û����Դ�Ļ���������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--;
	}

	// ����һ���ź�����Դ
	void post()
	{
		if (isExit_) // ��������ʼ�����˲���Ҫ��������
		{
			return;
		}
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		// Linux�µ�condition_variable����������δ���д���
		// ���´˴�״̬ʧЧ���޹�����
		cond_.notify_all(); // �ȴ�״̬���ͷ�mutex����֪ͨ��������wait�ĵط���������
	}

private:
	std::atomic_bool isExit_;
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

// Task���͵�ǰ������
class Task;

// ʵ�ֽ����ύ���̳߳ص�task����ִ����ɺ�ķ���ֵ����Result
class Result
{
public:
	// ���캯��
	Result(std::shared_ptr<Task> task, bool isValid = true);

	// ��������
	~Result() = default;

	// setVal��������ȡ����ִ����ɵķ���ֵ
	void setVal(Any any);

	// get�������û��������������ȡtask�ķ���ֵ
	Any get();

private:
	Any any_;  // �洢����ķ���ֵ
	Semaphore sem_; // �߳�ͨ���ź���
	std::shared_ptr<Task> task_; // ָ���Ӧ��ȡ����ֵ���������
	std::atomic_bool isValid_; // ����ֵ�Ƿ���Ч
};

// ����������
class Task
{
public:
	// ���캯��
	Task();

	// ��������
	~Task() = default;

	// ��ȡResult����
	void setResult(Result* res);

	// ִ�к���
	void exec();

	// �û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;

private:
	Result* result_; // Result ������������� > Task����Result�����У�����������
};

// �߳�����
class Thread
{
public:
	// �̺߳�����������
	using THreadFunc = std::function<void(int)>;

	// �̹߳���
	Thread(THreadFunc func);

	// �߳�����
	~Thread();

	// �����߳�
	void start();

	// ��ȡ�߳�id
	int getId() const;

private:
	THreadFunc func_;
	static int generateId_;
	int threadId_; // �����߳�id
};

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED, // �̶��������߳�
	MODE_CACHED, // �߳������ɶ�̬����
};

// �̳߳�����
class ThreadPool
{
public:
	// �̳߳ع���
	ThreadPool();

	// �̳߳�����
	~ThreadPool();

	// �����̳߳�
	void start(int initThreadSize = std::thread::hardware_concurrency());

	// �����̳߳صĹ���ģʽ
	void setMode(PoolMode mode);

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold_(int threshHold);

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	// ɾ���������캯��
	ThreadPool(const ThreadPool&) = delete;

	// ɾ����ֵ���������
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �̺߳���
	void threadFunc(int theadId);

	// ����̳߳ص�����״̬
	bool checkRunningState() const;

private:
	// std::vector<std::unique_ptr<Thread>> threads_; // �߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳�����
	int initThreadSize_; // ��ʼ���߳�����
	std::atomic_int idleThreadSize_; //�����̵߳�����
	std::atomic_int curThreadSize_; // ��¼��ǰ�̳߳������̵߳�������
	int threadSizeThreshHold_;// �߳�����������ֵ

	std::queue<std::shared_ptr<Task>> taskQue_; // �������
	std::atomic_int taskSize_; // ���������
	int taskQueMaxThreshHold_; // �����������������ֵ

	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
	std::condition_variable notFull_; // ��ʾ������в���
	std::condition_variable notEmpty_; // ��ʾ������в���
	std::condition_variable exitCond_; // �ȴ��߳���Դȫ������

	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
	std::atomic_bool isPoolRunning_;// ��ʾ��ǰ�̳߳ص�����״̬
};

#endif //  THREADPOOL_H
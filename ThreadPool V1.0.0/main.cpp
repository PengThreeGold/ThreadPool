#include <iostream>
#include <chrono>
#include <thread>
#include "threadpool.h"

using uLLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		:begin_(begin)
		, end_(end)
	{}

	// ����һ����ô���run�����ķ���ֵ�����Ա�ʾ���������
	// Java Python ������ Object�� ��������Ļ���
	// C++17�±�׼���� Any����

	Any run()
	{
		// Mater - Slave�߳�ģ�ͣ����̼߳��㣬���̻߳���
		std::cout << "tid: " << std::this_thread::get_id() << "begin!" << std::endl;

		std::this_thread::sleep_for(std::chrono::seconds(4));

		uLLong sum = 0;
		for (uLLong i = begin_; i <= end_; ++i)
		{
			sum += i;
		}

		std::cout << "tid: " << std::this_thread::get_id() << "end!" << std::endl;
		return sum;
	}

private:
	int begin_;
	int end_;
};

int main()
{
	{
		// ��������
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(2);

		Result res = pool.submitTask(std::make_shared<MyTask>(1, 10000));
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		Result res4 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res5 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res6 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
	}

	std::cout << "main over!" << std::endl;
	char exit = getchar();
	return 0;
}
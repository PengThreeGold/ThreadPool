# ThreadPool
线程池是提高多线程业务处理能力的必备模块，本项目模仿C++版本新特性设计线程池，支持任意任务与返回值，并编译成动态库支持任意运行环境，已成功在高并发网络服务器程序中部署。
+ 开发环境
  + `C++17` 及更新
  + `Linux` 环境下 `GCC` 版本建议 `V7`及以上
  + 本项目基于 `Visual Studio 2022` 开发
+ 必备知识
  + `C++11`及以上标准的面向对象编程
    + 继承、封装和多态、STL容器、智能指针、函数对象、绑定器、可变惨模板
  + `C++11` 语言级别多线程编程
    + thread、mutex、atomic、condition_variable、unique_lock等
  + `C++17` 和 `C++20` 新标准内容及其源码
    + 任意类型（any）、信号量（semaphore）
  + 操作系统多线程基本理论
    + 多线程、线程通信、线程安全、原子操作、CAS等
+ FIXED模式
  + 线程池内的线程个数是固定不变的。
  + 一般在线程池创建时根据当前机器的CPU核心数量决定。
  + 支持手动指定。
+ CACHED模式
  + 线程池内的线程个数是动态增长的。
  + 根据线程池任务队列的数量进行动态的增加，直到达到系统所设定的线程数量阈值。
  + 动态增长的线程空闲等待超过设定值（1min）则线程池会自动回收，直到回收至剩下的线程数量等于最初的线程数量。
+ 应用场景
  + 高并发网络服务器
  + `master-slave` 线程模型
  + 耗时任务处理

---

## ThreadPool V1.0.0
2023年3月25日发布更新日志
1. 支持Windows下运行；
2. 支持FIXED工作模式；
3. 支持CACHED工作模式；
4. 支持任意线程任务提交；
5. 支持任意线程任务参数返回；
6. 支持动态使用，自动/手动析构；

---

## ThreadPool V1.1.0
2023年3月30日发布更新日志
1. 解决了Linux环境下condition_variable库中析构函数为空导致的线程死锁问题

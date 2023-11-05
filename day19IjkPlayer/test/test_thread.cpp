// detach():会使线程失去我们的控制,但是如果希望子线程做的事情与我们控制与否不影响,那么
// 就可以使用detach(),因为子线程会跑到后台运行.
// 一旦调用了detach,就不要调用join,否则系统发生异常.
#include <iostream>
#include <thread>
using namespace std;

void thread_callback()
{
    cout << "这是子线程运行的函数" << endl;
}

void test_detach()
{
    // 执行到该函数,t1子线程将和主线程分离.即就是t1将在后台运行
    // 主线程不会和运行join一样在这里阻塞,而是正常退出
    thread t1(thread_callback);
    t1.detach();
}


int main()
{
    test_detach();
    return 0;
}


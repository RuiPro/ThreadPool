#include <iostream>
#include "ThreadPool/ThreadPool.hpp"

/* 某工厂生产一种食品，该产品有淡季和旺季。春秋是淡季，夏冬是旺季。
 * 当淡季时，订单数会有所下降，旺季则会激增
 * 为了最小化人力财政支出，该工厂除了几个长工外，其余均为临时工
 * 其中长工为3人，整个工厂最多只能有15个员工
 * 假设每个季节只有30秒，其中每一笔订单需5秒完成
 * 春季的订单速度为1.5秒每单，夏季为0.2秒每单，秋季为3秒每单，冬季为0.5秒每单
 * 那么春季总计20单，夏季150单，秋季10单，冬季60单
 * 预计需要人力春季4人，夏季25人，秋季2人，冬季10人
 */

void Order(int num) {
    printf("有一笔订单，订单编号：%d\n", num);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    printf("订单编号：%d 的订单完成！\n", num);
}

int main() {
    system("chcp 65001");
    ThreadPool tp(3, 30, 30);
    //tp.SafelyExit(false);
    //tp.ReceiveAllTask(false);
    tp.Start();
    printf("========================春季来临========================\n");
    for (int i = 0; i < 20; ++i) {
        int j = rand();
        if(tp.AddTask(&Order, j) == -1){
            printf("订单%d被退回\n",j);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
    printf("========================夏季来临========================\n");
    for (int i = 0; i < 150; ++i) {
        int j = rand();
        if(tp.AddTask(&Order, j) == -1){
            printf("订单%d被退回\n",j);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    printf("========================秋季来临========================\n");
    for (int i = 0; i < 10; ++i) {
        int j = rand();
        if(tp.AddTask(&Order, j) == -1){
            printf("订单%d被退回\n",j);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    printf("========================冬季来临========================\n");
    for (int i = 0; i < 60; ++i) {
        int j = rand();
        if(tp.AddTask(&Order, j) == -1){
            printf("订单%d被退回\n",j);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    printf("========================今年结束========================\n");
    return 0;
}

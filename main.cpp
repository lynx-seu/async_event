
#include "eventloop.h"
#include <iostream>

int main()
{
    lynx::EventLoop el;

    el.every(1000, lynx::MATH_HUGE, [](long long id) {
            static int i = 0;
            std::cout << i++ << std::endl;
    });

    el.after(30000, [&el](long long id) {
            el.stop();
            std::cout << "close all" << std::endl;
    });
    el.start();


	return 0;
}

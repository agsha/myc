// a single header file is required
#include <iostream>
#include <vector>
#include <ev.h>
#include <thread>

using namespace std;
struct x {
    int a;
    int b;
};
void prepare_cb(EV_P_ ev_prepare *w, int revents) {
//    cout<<"prepare cb"<<endl;
}
void check_cb(EV_P_ ev_check *w, int revents) {
//    cout<<"check cb"<<endl;

}
void check_cb1(EV_P_ ev_io *w, int revents) {
    cout<<"check_cb1 e cb"<<endl;

}
void check_cb2(EV_P_ ev_io *w, int revents) {
//    cout<<"chec_cb2 cb"<<endl;


}

struct foo {
    int a=10, b=10, c=10, d=10, e=10, f=10;
};
void do1() {
    struct ev_loop *loop = ev_loop_new();
    ev_prepare prepare;
    ev_prepare_init(&prepare, prepare_cb);
    ev_prepare_start(loop, &prepare);

    ev_check check;
    ev_check_init(&check, check_cb);
    ev_check_start(loop, &check);

    vector<ev_io> vec;

    vec.emplace_back();
    ev_io &stdin_watcher1 = vec[0];
//    ev_io stdin_watcher1;
    ev_io_init (&stdin_watcher1, check_cb1, /*STDIN_FILENO*/ 0, EV_READ);
    ev_io_start (loop, &stdin_watcher1);

    vec.emplace_back();
    ev_io &stdin_watcher2 = vec[1];
//    ev_io stdin_watcher2;
    ev_io_init (&stdin_watcher2, check_cb2, /*STDIN_FILENO*/ fileno(fopen("/dev/zero", "r")), EV_READ);
    ev_io_start (loop, &stdin_watcher2);

    thread t([loop](){
        ev_run(EV_A_ 0);
    });
    t.join();




}


void do2() {
    vector<foo> vec;
    vec.emplace_back();
    foo &f = vec[0];
    thread t([&f]() {

        int count = 0;
        for (int i = 0; i < 100; i++) {
            char buf[100];
            for (int j = 0; j < sizeof(buf) / sizeof(buf[0]); j++) {
                count += buf[j];
            }
        }
        cout<<"count "<<count<<endl;
        cout<<"from thread, expecting 10 but got "<<f.e<<endl;
    });
    int count = 0;
    for (int i = 0; i < 100000; i++) {
        vec.emplace_back();
    }

    t.join();
    cout<<"hi"<<endl;
}
int
main1(void)
{
    do2();
}

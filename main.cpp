// a single header file is required
#include <iostream>
#include <vector>

using namespace std;
struct x {
    int a;
    int b;
};
void do1() {
    vector<char> vec;
    int sz = 1024*1024;
    vec.reserve(sz);
    char *p = &vec[0];
    for(int i=0; i<sz; i++) {
        *(p+i) = 'b';
    }
    cout<<"fini"<<endl;
}
int
main1(void)
{
    do1();
}

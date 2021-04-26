#include <iostream>
#include "art.h"

using namespace std;

void test_init_and_destroy(){
    art_tree t;
    int res = art_tree_init(&t);

    if(res == 0)
        cout << "init success" << endl;
    else {
        cout<<"init failure" << endl;
        return;
    }

    res = art_tree_destroy(&t);
    if(res == 0)
        cout << "destroy success" << endl;
    else {
        cout<<"destroy failure" << endl;
        return;
    }
}

int main() {
//    test_init_and_destroy();

    return 0;
}

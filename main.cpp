#include <iostream>
#include "art.h"
#include <dbg.h>
#include <cassert>
#include <cstring>

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

void test_insert(){
    art_tree t;
    int res = art_tree_init(&t);
    if(res == 0){
        dbg("init success");
    } else {
        dbg("init failure");
        return;
    }

    int len;
    char buf[512];
    FILE *f = fopen("/home/chen/CLionProjects/art_test/int.txt", "r");

    uintptr_t line = 1;
    while (fgets(buf, sizeof buf, f)) {
        len = strlen(buf);
        buf[len-1] = '\0';
        if(art_insert(&t, (unsigned char *)buf, len,
                      (void*)line) == NULL){
            cout<<"insert failure"<<endl;
            return;
        }
        if(art_size(&t) == line){
            cout <<"art_size insert failure"<<endl;
            return;
        }
        line++;
    }
    res = art_tree_destroy(&t);
    if(res == 0){
        cout <<"art_tree destroy failure" <<endl;
    }
}


int main() {
//    test_init_and_destroy();
    dbg(sizeof(art_node256_leaf));
    test_insert();
    return 0;
}

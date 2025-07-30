class Foo {
    int32 m1, m2, m3;
};

void bar(int32 a, int32 b, int32 c) {
    print(""+a);
    print(""+b);
    print(""+c);
}

Foo global_foo;

void global_test() {
    global_foo.m1 = 123;
    global_foo.m2 = 456;
    global_foo.m3 = 789;

    bar(global_foo.m1, global_foo.m2, global_foo.m3);
}
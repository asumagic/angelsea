void benchmark_1M_calls_noarg() {
    for (int i = 0; i < 1000000; ++i) {
        noarg();
    }
}

void benchmark_1M_calls_method_arg() {
    SomeClass s;
    s.a = 0;
    for (int i = 0; i < 1000000; ++i) {
        s.add_obj(i);
    }
    print('' + s.a);
}
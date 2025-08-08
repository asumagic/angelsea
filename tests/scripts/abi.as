void benchmark_1M_calls_generic_noarg() {
    for (int i = 0; i < 1000000; ++i) {
        generic_noarg();
    }
}

void benchmark_1M_calls_generic_method_arg() {
    SomeClass s;
    s.a = 0;
    for (int i = 0; i < 1000000; ++i) {
        s.generic_add_obj(i);
    }
    print('' + s.a);
}
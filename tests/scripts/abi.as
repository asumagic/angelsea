void benchmark_1M_calls_generic_noarg() {
    for (int i = 0; i < 1000000; ++i) {
        generic_noarg();
    }
}
Base@ b2;

void foo() {
    @b2 = @b;
    b2.bar = 123;
    b.foo();
}
class Foo
{
    Foo() {}
    Foo(int a) { m_a = a; }

    int m_a;
};

void main()
{
    Foo[] foos;
    foos.insertLast(Foo(123));
    foos.insertLast(Foo(456));
    foos.insertLast(Foo(789));

    for (uint i = 0; i < foos.length(); ++i)
    {
        print(foos[i].m_a);
    }
}

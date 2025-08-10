class Base
{

};

class Derived : Base
{

};

class Unrelated
{

};

void downcast() {
    Derived d;
    Base@ b = @d;
    Derived@ d2 = cast<Derived>(b);
}

void broken_downcast() {
    Base b;
    Unrelated@ u = cast<Unrelated>(b);
}
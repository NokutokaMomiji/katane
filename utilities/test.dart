import 'dart:convert';

class MyClass {
    int number;
    int radix;

    MyClass(int a, int b) : number = a, radix = b {
        print(number);
    }
}

class Another {
    final int a;
    final String b;

    const Another(this.a, bool anotherParameter, this.b);
}

class Some extends Another {
    Some(super.a, super.anotherParameter, super.b);

    int param() => this.a;
}

class A {
    final a = Another(2, true, "false");

    A();
}

void main() {
    String string;
    int integer;
    double doubleNum;
    num number;
    bool boolean;
    List list;
    Map map;
    Iterable iterable;
    Iterator iterator;
    Encoding encoding;
    print(23);
    MyClass(23, 2).toString();
    A().a.a;
    return;
}

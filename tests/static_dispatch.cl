class A {
  who() : String { "A" };
};
class B inherits A {
  who() : String { "B" };
};
class Main inherits IO {
  main() : Object {
    let x : B <- new B in {
      out_string(x.who());   out_string("\n");
      out_string(x@A.who()); out_string("\n");
      out_string(x@B.who()); out_string("\n");
    }
  };
};

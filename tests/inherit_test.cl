class Animal {
  name() : String { "animal" };
  copy() : SELF_TYPE { self };
  describe() : String { name().concat("!") };
};
class Dog inherits Animal {
  name() : String { "dog" };
};
class Main inherits IO {
  main() : Object {
    let d : Dog <- new Dog in {
      out_string(d.describe());         out_string("\n");
      out_string(d.copy().name());      out_string("\n");
      out_string(d.type_name());        out_string("\n");
      out_string(d.copy().type_name()); out_string("\n");
    }
  };
};

class Main inherits IO {
  fact(n : Int) : Int {
    if n = 0 then 1 else n * fact(n - 1) fi
  };
  main() : Object {
    {
      out_int(fact(5));
      out_string("\n");
    }
  };
};

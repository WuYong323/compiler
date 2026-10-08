class Main inherits IO {
  main() : Object {
    {
      out_string("name? ");
      out_string(in_string());
      out_string("\nnum? ");
      out_int(in_int());
      out_string("\n--- 反过来：先 int 再 string ---\n");
      out_string("num? ");
      out_int(in_int());
      out_string("\nname? ");
      out_string(in_string());
      out_string("\n");
    }
  };
};

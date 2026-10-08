class Main inherits IO {
  main() : Object {
    let s : String <- "Hello" in {
      out_int(s.length());            out_string("\n");
      out_string(s.concat(" World")); out_string("\n");
      out_string(s.substr(1, 3));     out_string("\n");
    }
  };
};

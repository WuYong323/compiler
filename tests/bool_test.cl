class Main inherits IO {
  main() : Object {
    {
      out_string((1 < 2).type_name());             out_string("\n");
      out_string((1 = 1).type_name());             out_string("\n");
      out_string((not false).type_name());         out_string("\n");
      out_string((isvoid new Object).type_name()); out_string("\n");
      if 1 < 2 then out_string("yes\n") else out_string("no\n") fi;
      if not false then out_string("notfalse\n") else out_string("bad\n") fi;
      if "abc" < "abd" then out_string("strlt\n") else out_string("bad\n") fi;
      if 2 <= 2 then out_string("le\n") else out_string("bad\n") fi;
    }
  };
};

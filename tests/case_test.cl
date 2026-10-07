class Main inherits IO {
  describe(x : Object) : String {
    case x of
      i : Int    => "Int";
      s : String => "String";
      b : Bool   => "Bool";
      o : Object => "other";
    esac
  };
  main() : Object {
    {
      out_string(describe(42));       out_string("\n");
      out_string(describe("hi"));     out_string("\n");
      out_string(describe(true));     out_string("\n");
      out_string(describe(1 = 1));    out_string("\n");
      out_string(describe(new Main)); out_string("\n");
    }
  };
};

(* nested comment (* still a comment *) done *)
-- line comment
class Main inherits IO {
  name : String <- "a\"b\\c\n\t";
  n : Int <- 12345;
  flag : Bool <- false;
  main() : Object {
    let x : Int <- n + 1, y : Int <- 2 in
      if x <= y then ~x else x * y fi
  };
};

class Main inherits IO {
  main() : Object {
    let i : Int <- 0, sum : Int <- 0 in {
      while i < 5 loop {
        sum <- sum + i;
        i <- i + 1;
      } pool;
      out_int(sum);
      out_string("\n");
    }
  };
};

class List {
  isNil() : Bool { true };
  head() : Int { 0 };
  tail() : List { self };
};
class Nil inherits List { };
class Cons inherits List {
  xcar : Int;
  xcdr : List;
  isNil() : Bool { false };
  head() : Int { xcar };
  tail() : List { xcdr };
  init(hd : Int, tl : List) : SELF_TYPE {
    { xcar <- hd; xcdr <- tl; self; }
  };
};
class Main inherits IO {
  sum(l : List) : Int {
    if l.isNil() then 0 else l.head() + sum(l.tail()) fi
  };
  main() : Object {
    let l : List <- (new Cons).init(3, (new Cons).init(2, new Nil)) in
      out_int(sum(l))
  };
};

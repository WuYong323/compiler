#include "parser.h"
#include <stdexcept>

namespace cool {

[[noreturn]] void Parser::error(const std::string& msg) const {
  throw std::runtime_error("line " + std::to_string(cur_.line) + ": " + msg);
}

Token Parser::expect(Tok k, const std::string& what) {
  if (!at(k))
    error("expected " + what + " but got '" + cur_.lexeme + "'");
  Token t = cur_;
  advance();
  return t;
}

Program Parser::parseProgram() {
  Program prog;
  while (!at(Tok::END)) prog.classes.push_back(parseClass());
  return prog;
}

std::unique_ptr<ClassDef> Parser::parseClass() {
  auto c = std::make_unique<ClassDef>();
  Token cls = expect(Tok::CLASS, "'class'");
  c->line = cls.line;
  c->name = expect(Tok::TYPEID, "class name").lexeme;
  if (at(Tok::INHERITS)) {
    advance();
    c->parent = expect(Tok::TYPEID, "parent class name").lexeme;
  } else {
    c->parent = "Object";
  }
  expect(Tok::LBRACE, "'{'");
  while (!at(Tok::RBRACE)) c->features.push_back(parseFeature());
  expect(Tok::RBRACE, "'}'");
  expect(Tok::SEMI, "';'");
  return c;
}

std::unique_ptr<Feature> Parser::parseFeature() {
  Token nameTok = expect(Tok::OBJECTID, "feature name");
  if (at(Tok::LPAREN)) {
    // 方法
    auto m = std::make_unique<Method>();
    m->line = nameTok.line;
    m->name = nameTok.lexeme;
    advance(); // '('
    if (!at(Tok::RPAREN)) {
      for (;;) {
        Formal f;
        f.name = expect(Tok::OBJECTID, "formal name").lexeme;
        expect(Tok::COLON, "':'");
        f.type = expect(Tok::TYPEID, "formal type").lexeme;
        m->formals.push_back(std::move(f));
        if (!at(Tok::COMMA)) break;
        advance();
      }
    }
    expect(Tok::RPAREN, "')'");
    expect(Tok::COLON, "':'");
    m->type = expect(Tok::TYPEID, "return type").lexeme;
    expect(Tok::LBRACE, "'{'");
    m->body = parseExpr();
    expect(Tok::RBRACE, "'}'");
    expect(Tok::SEMI, "';'");
    return m;
  } else {
    // 属性
    auto a = std::make_unique<Attribute>();
    a->line = nameTok.line;
    a->name = nameTok.lexeme;
    expect(Tok::COLON, "':'");
    a->type = expect(Tok::TYPEID, "attribute type").lexeme;
    if (at(Tok::ASSIGN)) {
      advance();
      a->init = parseExpr();
    }
    expect(Tok::SEMI, "';'");
    return a;
  }
}

// ================= 表达式（优先级分层） =================

std::unique_ptr<Expr> Parser::parseExpr() {
  // 赋值 ID <- expr（最松，右结合；左部必须是 OBJECTID）
  if (at(Tok::OBJECTID) && nxt_.kind == Tok::ASSIGN) {
    Token id = cur_;
    advance();          // 消费 ID
    advance();          // 消费 <-
    auto a = std::make_unique<Assign>();
    a->line = id.line;
    a->name = id.lexeme;
    a->rhs = parseExpr();
    return a;
  }
  return parseNot();
}

std::unique_ptr<Expr> Parser::parseNot() {
  if (at(Tok::NOT)) {
    Token t = cur_; advance();
    auto n = std::make_unique<Not>();
    n->line = t.line;
    n->expr = parseNot();
    return n;
  }
  return parseCompare();
}

std::unique_ptr<Expr> Parser::parseCompare() {
  auto lhs = parseAddSub();
  BinOp op;
  if (at(Tok::LE)) op = BinOp::Le;
  else if (at(Tok::LT)) op = BinOp::Lt;
  else if (at(Tok::EQ)) op = BinOp::Eq;
  else return lhs;
  Token t = cur_; advance();
  auto b = std::make_unique<Binary>();
  b->line = t.line; b->op = op;
  b->lhs = std::move(lhs);
  b->rhs = parseAddSub();
  return b;
}

std::unique_ptr<Expr> Parser::parseAddSub() {
  auto lhs = parseMulDiv();
  while (at(Tok::PLUS) || at(Tok::MINUS)) {
    BinOp op = at(Tok::PLUS) ? BinOp::Plus : BinOp::Minus;
    Token t = cur_; advance();
    auto b = std::make_unique<Binary>();
    b->line = t.line; b->op = op;
    b->lhs = std::move(lhs);
    b->rhs = parseMulDiv();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseMulDiv() {
  auto lhs = parseIsvoid();
  while (at(Tok::STAR) || at(Tok::SLASH)) {
    BinOp op = at(Tok::STAR) ? BinOp::Mul : BinOp::Div;
    Token t = cur_; advance();
    auto b = std::make_unique<Binary>();
    b->line = t.line; b->op = op;
    b->lhs = std::move(lhs);
    b->rhs = parseIsvoid();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseIsvoid() {
  if (at(Tok::ISVOID)) {
    Token t = cur_; advance();
    auto iv = std::make_unique<IsVoid>();
    iv->line = t.line;
    iv->expr = parseNeg();
    return iv;
  }
  return parseNeg();
}

std::unique_ptr<Expr> Parser::parseNeg() {
  if (at(Tok::TILDE)) {
    Token t = cur_; advance();
    auto n = std::make_unique<Neg>();
    n->line = t.line;
    n->expr = parseNeg();
    return n;
  }
  return parseDispatch();
}

std::unique_ptr<Expr> Parser::parseDispatch() {
  auto e = parsePrimary();
  for (;;) {
    if (at(Tok::DOT)) {
      Token t = cur_; advance();
      std::string method = expect(Tok::OBJECTID, "method name").lexeme;
      auto d = std::make_unique<Dispatch>();
      d->line = t.line;
      d->receiver = std::move(e);
      d->method = method;
      d->args = parseDispatchArgs();
      e = std::move(d);
    } else if (at(Tok::AT)) {
      Token t = cur_; advance();
      std::string ty = expect(Tok::TYPEID, "type name").lexeme;
      expect(Tok::DOT, "'.'");
      std::string method = expect(Tok::OBJECTID, "method name").lexeme;
      auto d = std::make_unique<StaticDispatch>();
      d->line = t.line;
      d->receiver = std::move(e);
      d->type = ty;
      d->method = method;
      d->args = parseDispatchArgs();
      e = std::move(d);
    } else {
      break;
    }
  }
  return e;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
  Token t = cur_;
  switch (cur_.kind) {
    case Tok::OBJECTID: {
      advance();
      if (at(Tok::LPAREN)) {
        // self-dispatch：ID(args) == self.ID(args)
        advance();
        auto d = std::make_unique<Dispatch>();
        d->line = t.line;
        auto self = std::make_unique<Object>();
        self->name = "self"; self->line = t.line;
        d->receiver = std::move(self);
        d->method = t.lexeme;
        if (!at(Tok::RPAREN)) {
          for (;;) {
            d->args.push_back(parseExpr());
            if (!at(Tok::COMMA)) break;
            advance();
          }
        }
        expect(Tok::RPAREN, "')'");
        return d;
      }
      auto o = std::make_unique<Object>();
      o->name = t.lexeme; o->line = t.line;
      return o;
    }
    case Tok::INT_CONST: {
      advance();
      auto n = std::make_unique<IntConst>();
      n->lexeme = t.lexeme;
      n->value = std::stoi(t.lexeme);
      n->line = t.line;
      return n;
    }
    case Tok::STR_CONST: {
      advance();
      auto s = std::make_unique<StringConst>();
      s->value = t.lexeme;
      s->line = t.line;
      return s;
    }
    case Tok::TRUE: {
      advance();
      auto b = std::make_unique<BoolConst>();
      b->value = true; b->line = t.line;
      return b;
    }
    case Tok::FALSE: {
      advance();
      auto b = std::make_unique<BoolConst>();
      b->value = false; b->line = t.line;
      return b;
    }
    case Tok::LPAREN: {
      advance();
      auto e = parseExpr();
      expect(Tok::RPAREN, "')'");
      return e;
    }
    case Tok::IF: return parseIf();
    case Tok::WHILE: return parseWhile();
    case Tok::LBRACE: return parseBlock();
    case Tok::LET: return parseLet();
    case Tok::CASE: return parseCase();
    case Tok::NEW: {
      advance();
      auto n = std::make_unique<New>();
      n->line = t.line;
      n->type = expect(Tok::TYPEID, "type name").lexeme;
      return n;
    }
    default:
      error("unexpected token '" + cur_.lexeme + "'");
  }
}

std::unique_ptr<Expr> Parser::parseIf() {
  Token t = cur_; advance(); // if
  auto n = std::make_unique<If>(); n->line = t.line;
  n->cond = parseExpr();
  expect(Tok::THEN, "'then'");
  n->then_ = parseExpr();
  expect(Tok::ELSE, "'else'");
  n->else_ = parseExpr();
  expect(Tok::FI, "'fi'");
  return n;
}

std::unique_ptr<Expr> Parser::parseWhile() {
  Token t = cur_; advance(); // while
  auto n = std::make_unique<While>(); n->line = t.line;
  n->cond = parseExpr();
  expect(Tok::LOOP, "'loop'");
  n->body = parseExpr();
  expect(Tok::POOL, "'pool'");
  return n;
}

std::unique_ptr<Expr> Parser::parseBlock() {
  Token t = cur_; advance(); // {
  auto n = std::make_unique<Block>(); n->line = t.line;
  while (!at(Tok::RBRACE)) {
    n->exprs.push_back(parseExpr());
    expect(Tok::SEMI, "';'");
  }
  expect(Tok::RBRACE, "'}'");
  return n;
}

std::unique_ptr<Expr> Parser::parseLet() {
  Token t = cur_; advance(); // let
  auto n = std::make_unique<Let>(); n->line = t.line;
  for (;;) {
    Binding b;
    b.name = expect(Tok::OBJECTID, "variable name").lexeme;
    expect(Tok::COLON, "':'");
    b.type = expect(Tok::TYPEID, "type name").lexeme;
    if (at(Tok::ASSIGN)) { advance(); b.init = parseExpr(); }
    n->bindings.push_back(std::move(b));
    if (!at(Tok::COMMA)) break;
    advance();
  }
  expect(Tok::IN, "'in'");
  n->body = parseExpr();
  return n;
}

std::unique_ptr<Expr> Parser::parseCase() {
  Token t = cur_; advance(); // case
  auto n = std::make_unique<Case>(); n->line = t.line;
  n->scrutinee = parseExpr();
  expect(Tok::OF, "'of'");
  while (!at(Tok::ESAC)) {
    CaseBranch b;
    b.name = expect(Tok::OBJECTID, "branch variable").lexeme;
    expect(Tok::COLON, "':'");
    b.type = expect(Tok::TYPEID, "branch type").lexeme;
    expect(Tok::DARROW, "'=>'");
    b.body = parseExpr();
    expect(Tok::SEMI, "';'");
    n->branches.push_back(std::move(b));
  }
  expect(Tok::ESAC, "'esac'");
  return n;
}

std::vector<std::unique_ptr<Expr>> Parser::parseDispatchArgs() {
  expect(Tok::LPAREN, "'('");
  std::vector<std::unique_ptr<Expr>> args;
  if (!at(Tok::RPAREN)) {
    for (;;) {
      args.push_back(parseExpr());
      if (!at(Tok::COMMA)) break;
      advance();
    }
  }
  expect(Tok::RPAREN, "')'");
  return args;
}

} // namespace cool

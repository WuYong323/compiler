#include "lexer.h"
#include <cctype>

namespace cool {

char Lexer::advance() {
  if (atEnd()) return '\0';
  char c = src_[pos_++];
  if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
  return c;
}

// 跳过空白与两种注释；发现未闭合注释时设置 error_
void Lexer::skipWhitespaceAndComments() {
  while (true) {
    while (!atEnd() && std::isspace((unsigned char)peek())) advance();
    if (atEnd()) return;
    // 行注释 "--"
    if (peek() == '-' && peek2() == '-') {
      advance(); advance();
      while (!atEnd() && peek() != '\n') advance();
      continue;
    }
    // 块注释 "(* ... *)"，可嵌套
    if (peek() == '(' && peek2() == '*') {
      int depth = 0;
      while (!atEnd()) {
        if (peek() == '(' && peek2() == '*') { advance(); advance(); ++depth; }
        else if (peek() == '*' && peek2() == ')') {
          advance(); advance(); --depth;
          if (depth == 0) break;
        }
        else advance();
      }
      if (depth != 0) { error_ = "EOF in comment"; return; }
      continue;
    }
    return; // 下一个 token 的起始位置
  }
}

// 关键字判定（大小写不敏感；true/false 首字符必须小写）
static bool keywordKind(const std::string& s, Tok& out) {
  std::string low; low.reserve(s.size());
  for (char c : s) low.push_back((char)std::tolower((unsigned char)c));
  if (low == "true")  { if (s[0] == 't') { out = Tok::TRUE;  return true; } return false; }
  if (low == "false") { if (s[0] == 'f') { out = Tok::FALSE; return true; } return false; }
  static const std::pair<const char*, Tok> kws[] = {
    {"class", Tok::CLASS}, {"else", Tok::ELSE}, {"fi", Tok::FI}, {"if", Tok::IF},
    {"in", Tok::IN}, {"inherits", Tok::INHERITS}, {"isvoid", Tok::ISVOID},
    {"let", Tok::LET}, {"loop", Tok::LOOP}, {"pool", Tok::POOL}, {"then", Tok::THEN},
    {"while", Tok::WHILE}, {"case", Tok::CASE}, {"esac", Tok::ESAC},
    {"new", Tok::NEW}, {"of", Tok::OF}, {"not", Tok::NOT},
  };
  for (auto& kw : kws)
    if (low == kw.first) { out = kw.second; return true; }
  return false;
}

Token Lexer::readIdentifier() {
  int sl = line_, sc = col_;
  std::string s; s += advance();
  while (!atEnd() && (std::isalnum((unsigned char)peek()) || peek() == '_')) s += advance();
  Tok k;
  if (keywordKind(s, k)) return {k, s, sl, sc};
  if (std::isupper((unsigned char)s[0])) return {Tok::TYPEID, s, sl, sc};
  return {Tok::OBJECTID, s, sl, sc};
}

Token Lexer::readNumber() {
  int sl = line_, sc = col_;
  std::string s;
  while (!atEnd() && std::isdigit((unsigned char)peek())) s += advance();
  // 32 位有符号非负上限 2147483647：先比长度，再比字典序
  if (s.size() > 10 || (s.size() == 10 && s > "2147483647")) {
    error_ = "Integer constant too large";
    return {Tok::ERROR, s, sl, sc};
  }
  return {Tok::INT_CONST, s, sl, sc};
}

Token Lexer::readString() {
  int sl = line_, sc = col_;
  advance(); // 消费开头的 "
  std::string val;
  while (true) {
    if (atEnd()) { error_ = "EOF in string constant"; return {Tok::ERROR, val, sl, sc}; }
    char c = advance();
    if (c == '"') break;                       // 正常结束
    if (c == '\n') { error_ = "Unterminated string constant"; return {Tok::ERROR, val, sl, sc}; }
    if (c == '\0') { error_ = "String contains NUL character"; return {Tok::ERROR, val, sl, sc}; }
    if (c == '\\') {                           // 反斜杠：吃掉下一个字符，两个字面都保留
      if (atEnd()) { error_ = "EOF in string constant"; return {Tok::ERROR, val, sl, sc}; }
      char d = advance();
      if (d == '\n') { error_ = "Unterminated string constant"; return {Tok::ERROR, val, sl, sc}; }
      val += c; val += d;                      // \t \n 也不翻译，交给运行时
      continue;
    }
    val += c;
  }
  if (val.size() > 1024) { error_ = "String constant too long"; return {Tok::ERROR, val, sl, sc}; }
  return {Tok::STR_CONST, val, sl, sc};
}

Token Lexer::readOperator() {
  int sl = line_, sc = col_;
  char c = advance();
  switch (c) {
    case '{': return {Tok::LBRACE, "{", sl, sc};
    case '}': return {Tok::RBRACE, "}", sl, sc};
    case '(': return {Tok::LPAREN, "(", sl, sc};
    case ')': return {Tok::RPAREN, ")", sl, sc};
    case ':': return {Tok::COLON, ":", sl, sc};
    case ';': return {Tok::SEMI, ";", sl, sc};
    case ',': return {Tok::COMMA, ",", sl, sc};
    case '.': return {Tok::DOT, ".", sl, sc};
    case '@': return {Tok::AT, "@", sl, sc};
    case '~': return {Tok::TILDE, "~", sl, sc};
    case '+': return {Tok::PLUS, "+", sl, sc};
    case '-': return {Tok::MINUS, "-", sl, sc};
    case '*': return {Tok::STAR, "*", sl, sc};
    case '/': return {Tok::SLASH, "/", sl, sc};
    case '<':
      if (peek() == '-') { advance(); return {Tok::ASSIGN, "<-", sl, sc}; }
      if (peek() == '=') { advance(); return {Tok::LE, "<=", sl, sc}; }
      return {Tok::LT, "<", sl, sc};
    case '=':
      if (peek() == '>') { advance(); return {Tok::DARROW, "=>", sl, sc}; }
      return {Tok::EQ, "=", sl, sc};
    default:
      error_ = std::string("Unrecognized character '") + c + "'";
      return {Tok::ERROR, std::string(1, c), sl, sc};
  }
}

Token Lexer::next() {
  skipWhitespaceAndComments();
  if (!error_.empty()) {
    Token t{Tok::ERROR, error_, line_, col_};
    error_.clear();
    return t;
  }
  if (atEnd()) return {Tok::END, "", line_, col_};
  char c = peek();
  if (std::isalpha((unsigned char)c)) return readIdentifier();
  if (std::isdigit((unsigned char)c)) return readNumber();
  if (c == '"') return readString();
  return readOperator();
}

} // namespace cool
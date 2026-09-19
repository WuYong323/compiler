// 词法分析器（Lexer / Scanner）的类声明
// 把 Cool 源代码的字符流，切分成一个个 Token

#pragma once
#include <string>
#include <utility>
#include "token.h"

namespace cool {
    class Lexer {
        public:
            // 按值持有源码（move），避免持有 ss.str() 临时对象的悬垂引用
            explicit Lexer(std::string src) : src_(std::move(src)) {
            // 跳过 UTF-8 BOM（EF BB BF）：部分编辑器/工具会在文件头写入
            if (src_.size() >= 3 && (unsigned char)src_[0] == 0xEF &&
                (unsigned char)src_[1] == 0xBB && (unsigned char)src_[2] == 0xBF) {
                pos_ = 3;
            }
        }
        Token next();                 // 返回下一个 token（EOF 时为 END）

        private:
            std::string src_;
            size_t pos_ = 0;
            int line_ = 1;
            int col_ = 1;
            std::string error_;           // 待报错误（非空表示有错）

            bool atEnd() const { return pos_ >= src_.size(); }
            char peek() const { return atEnd() ? '\0' : src_[pos_]; }
            char peek2() const { return pos_ + 1 >= src_.size() ? '\0' : src_[pos_ + 1]; }
            char advance();               // 消费一个字符，维护行/列

            void skipWhitespaceAndComments();
            Token readIdentifier();
            Token readNumber();
            Token readString();
            Token readOperator();
    };

} // namespace cool




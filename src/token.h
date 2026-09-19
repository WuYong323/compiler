//定义 Token 的数据结构

#pragma once
#include <string>

namespace cool {
    // Token 类别
    enum class Tok {
        // 关键字
        CLASS, ELSE, FI, IF, IN, INHERITS, ISVOID, LET, LOOP, POOL, THEN,
        WHILE, CASE, ESAC, NEW, OF, NOT, TRUE, FALSE,
        // 标识符 / 字面量
        TYPEID, OBJECTID, INT_CONST, STR_CONST,
        // 标点 / 运算符
        LBRACE, RBRACE, LPAREN, RPAREN, COLON, SEMI, COMMA,
        ASSIGN, DOT, AT, TILDE, PLUS, MINUS, STAR, SLASH,
        LE, LT, EQ, DARROW,
        // 元
        END, ERROR
    };

    // 类别名（用于打印 token 流）
    inline const char* tokName(Tok k) {
        switch (k) {
            case Tok::CLASS: return "CLASS"; case Tok::ELSE: return "ELSE";
            case Tok::FI: return "FI"; case Tok::IF: return "IF";
            case Tok::IN: return "IN"; case Tok::INHERITS: return "INHERITS";
            case Tok::ISVOID: return "ISVOID"; case Tok::LET: return "LET";
            case Tok::LOOP: return "LOOP"; case Tok::POOL: return "POOL";
            case Tok::THEN: return "THEN"; case Tok::WHILE: return "WHILE";
            case Tok::CASE: return "CASE"; case Tok::ESAC: return "ESAC";
            case Tok::NEW: return "NEW"; case Tok::OF: return "OF";
            case Tok::NOT: return "NOT"; case Tok::TRUE: return "TRUE";
            case Tok::FALSE: return "FALSE";
            case Tok::TYPEID: return "TYPEID"; case Tok::OBJECTID: return "OBJECTID";
            case Tok::INT_CONST: return "INT_CONST"; case Tok::STR_CONST: return "STR_CONST";
            case Tok::LBRACE: return "LBRACE"; case Tok::RBRACE: return "RBRACE";
            case Tok::LPAREN: return "LPAREN"; case Tok::RPAREN: return "RPAREN";
            case Tok::COLON: return "COLON"; case Tok::SEMI: return "SEMI";
            case Tok::COMMA: return "COMMA"; case Tok::ASSIGN: return "ASSIGN";
            case Tok::DOT: return "DOT"; case Tok::AT: return "AT";
            case Tok::TILDE: return "TILDE"; case Tok::PLUS: return "PLUS";
            case Tok::MINUS: return "MINUS"; case Tok::STAR: return "STAR";
            case Tok::SLASH: return "SLASH"; case Tok::LE: return "LE";
            case Tok::LT: return "LT"; case Tok::EQ: return "EQ";
            case Tok::DARROW: return "DARROW";
            case Tok::END: return "END"; case Tok::ERROR: return "ERROR";
        }
        return "?";
    }


    // 一个词元
    struct Token {
        Tok kind= Tok::ERROR; // Token 的类型
        std::string lexeme; // Token 的值
        int line; // Token 所在的行号
        int column; // Token 所在的列号
    };

}// namespace cool
// src/main.cpp
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <exception>

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "dump.h" 

int main(int argc, char* argv[]) {
    // ① 参数检查
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source-file>\n";
        return 1;
    }

    // ② 读取源文件
    std::ifstream fin(argv[1]);
    if (!fin) {
        std::cerr << "Cannot open file: " << argv[1] << "\n";
        return 1;
    }
    std::stringstream buf;
    buf << fin.rdbuf();
    std::string source = buf.str();

    // ③ 词法分析 + 语法分析 + 打印
    try {
        // 注意：Parser 构造函数直接接收 std::string 源码
        cool::Parser parser(source); 
        
        // 注意：返回的是 Program 对象（值），不是 unique_ptr
        cool::Program program = parser.parseProgram();

        // 注意：调用独立函数 dumpProgram，且传入的是对象引用
        cool::dumpProgram(program, std::cout);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
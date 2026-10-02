/**
 * @file tokenizer.hpp
 * @brief 公开接口：Tokenizer + ASTNode（让 evaluator 可访问）
 *
 * ASTNode 必须在这里完整声明成 struct，evaluator.cpp 才能正常 include。
 */
#pragma once

#include <vector>
#include <memory>
#include <string>

namespace memoria {
namespace script {

/* ---------- Token ---------- */
struct Token {
    enum class Kind {
        Ident, IntLit, FloatLit, StringLit, CharLit,
        Plus, Minus, Star, Slash, Percent,
        Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
        Eq, Neq, Lt, Gt, Leq, Geq,
        Inc, Dec, And, Or, Not,
        LBrace, RBrace, LParen, RParen, LBracket, RBracket,
        Semicolon, Comma, Dot, Colon, Question, Arrow,
        If, Else, While, For, Return, Break, Continue,
        True, False, Null, This, Var, Function, New, Delete, Import, Include,
        Error, Eof,
    };
    Kind kind;
    std::string text;
    int line = 0; int col = 0;
};

std::vector<Token> tokenize(const std::string& src);

/* ---------- ASTNode（跨 .cpp 共享） ---------- */
struct ASTNode {
    enum class Type {
        Program, Function, Block,
        VarDecl, If, While, For, Return, Break, Continue,
        Assign, Binary, Unary, Call,
        Ident, IntLit, FloatLit, StringLit, BoolLit, NullLit,
    };
    Type type = Type::NullLit;
    std::string value;
    std::vector<std::unique_ptr<ASTNode>> children;
};

/* Parser 公开入口（内部类 Parser 隐藏在 .cpp）；err 非空时解析失败返回 nullptr 并回填原因 */
std::unique_ptr<ASTNode> parse_program(std::vector<Token> tokens, std::string* err = nullptr);

} }
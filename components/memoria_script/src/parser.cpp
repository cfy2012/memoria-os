/**
 * @file parser.cpp
 * @brief memoria::script Parser (递归下降)
 *
 * 纯 C++，零 ESP-IDF 依赖。
 * Token stream → AST。
 */

#include "tokenizer.hpp"
#include <vector>
#include <memory>
#include <string>

/* ASTNode 已在 tokenizer.hpp 里定义：这里只做 Parser 内部类 */
namespace memoria {
namespace script {

/* Parser 内部类实现 */
class Parser {
public:
    explicit Parser(std::vector<Token> t) : tokens_(std::move(t)) {}

    bool has_error() const { return error_; }
    const std::string& error_msg() const { return error_msg_; }

    std::unique_ptr<ASTNode> parse_program() {
        auto root = std::make_unique<ASTNode>();
        root->type = ASTNode::Type::Program;
        /* Error token 置错后终止解析，不再继续解析后续代码 */
        while (!check(Token::Kind::Eof) && !error_) {
            auto stmt = parse_stmt();
            if (stmt) root->children.push_back(std::move(stmt));
        }
        return root;
    }

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    bool error_ = false;
    std::string error_msg_;

    const Token& peek() { return tokens_[std::min(pos_, tokens_.size() - 1)]; }
    const Token& advance() { return tokens_[std::min(pos_++, tokens_.size() - 1)]; }
    bool check(Token::Kind k) { return peek().kind == k; }
    bool match(Token::Kind k) { if (check(k)) { advance(); return true; } return false; }

    std::unique_ptr<ASTNode> parse_stmt() {
        if (match(Token::Kind::Var)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::VarDecl;
            if (check(Token::Kind::Ident)) {
                node->value = advance().text;
            }
            if (match(Token::Kind::Assign)) {
                node->children.push_back(parse_expr());
            }
            match(Token::Kind::Semicolon);
            return node;
        }
        if (match(Token::Kind::If)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::If;
            if (match(Token::Kind::LParen)) node->children.push_back(parse_expr());
            if (match(Token::Kind::RParen)) {}
            /* 简化：只解析一个块或单语句 */
            if (match(Token::Kind::LBrace)) {
                auto block = std::make_unique<ASTNode>();
                block->type = ASTNode::Type::Block;
                while (!check(Token::Kind::RBrace) && !check(Token::Kind::Eof) && !error_)
                    block->children.push_back(parse_stmt());
                match(Token::Kind::RBrace);
                node->children.push_back(std::move(block));
            } else {
                node->children.push_back(parse_stmt());
            }
            if (match(Token::Kind::Else)) {
                if (match(Token::Kind::LBrace)) {
                    auto block = std::make_unique<ASTNode>();
                    block->type = ASTNode::Type::Block;
                    while (!check(Token::Kind::RBrace) && !check(Token::Kind::Eof) && !error_)
                        block->children.push_back(parse_stmt());
                    match(Token::Kind::RBrace);
                    node->children.push_back(std::move(block));
                } else {
                    node->children.push_back(parse_stmt());
                }
            }
            return node;
        }
        if (match(Token::Kind::While)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::While;
            match(Token::Kind::LParen);
            node->children.push_back(parse_expr());   /* 条件 */
            match(Token::Kind::RParen);
            if (match(Token::Kind::LBrace)) {
                auto block = std::make_unique<ASTNode>();
                block->type = ASTNode::Type::Block;
                while (!check(Token::Kind::RBrace) && !check(Token::Kind::Eof) && !error_)
                    block->children.push_back(parse_stmt());
                match(Token::Kind::RBrace);
                node->children.push_back(std::move(block));
            } else {
                node->children.push_back(parse_stmt());
            }
            return node;
        }
        if (match(Token::Kind::For)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::For;
            match(Token::Kind::LParen);
            /* init：var 声明（var i=0）或赋值表达式（i=0） */
            if (match(Token::Kind::Var)) {
                auto init = std::make_unique<ASTNode>();
                init->type = ASTNode::Type::VarDecl;
                if (check(Token::Kind::Ident)) init->value = advance().text;
                if (match(Token::Kind::Assign)) init->children.push_back(parse_expr());
                node->children.push_back(std::move(init));
            } else {
                node->children.push_back(parse_expr());
            }
            match(Token::Kind::Semicolon);
            node->children.push_back(parse_expr());   /* 条件 */
            match(Token::Kind::Semicolon);
            node->children.push_back(parse_expr());   /* 步进 */
            match(Token::Kind::RParen);
            if (match(Token::Kind::LBrace)) {
                auto block = std::make_unique<ASTNode>();
                block->type = ASTNode::Type::Block;
                while (!check(Token::Kind::RBrace) && !check(Token::Kind::Eof) && !error_)
                    block->children.push_back(parse_stmt());
                match(Token::Kind::RBrace);
                node->children.push_back(std::move(block));
            } else {
                node->children.push_back(parse_stmt());
            }
            return node;
        }
        if (match(Token::Kind::Return)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Return;
            if (!check(Token::Kind::Semicolon) && !check(Token::Kind::RBrace) && !check(Token::Kind::Eof))
                node->children.push_back(parse_expr());
            match(Token::Kind::Semicolon);
            return node;
        }
        if (match(Token::Kind::Break)) {
            match(Token::Kind::Semicolon);
            auto n = std::make_unique<ASTNode>();
            n->type = ASTNode::Type::Break;
            return n;
        }
        if (match(Token::Kind::Continue)) {
            match(Token::Kind::Semicolon);
            auto n = std::make_unique<ASTNode>();
            n->type = ASTNode::Type::Continue;
            return n;
        }
        /* 默认：表达式语句 */
        auto node = std::make_unique<ASTNode>();
        node->type = ASTNode::Type::Block;
        node->children.push_back(parse_expr());
        match(Token::Kind::Semicolon);
        return node;
    }

    /* Precedence climbing parser */
    std::unique_ptr<ASTNode> parse_expr() {
        return parse_assignment();
    }
    std::unique_ptr<ASTNode> parse_assignment() {
        auto left = parse_or();
        if (check(Token::Kind::Assign) || check(Token::Kind::PlusAssign)
            || check(Token::Kind::MinusAssign)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Assign;
            node->value = op.text;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_assignment());
            return node;
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_or() {
        auto left = parse_and();
        while (match(Token::Kind::Or)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = "||";
            node->children.push_back(std::move(left));
            node->children.push_back(parse_and());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_and() {
        auto left = parse_equality();
        while (match(Token::Kind::And)) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = "&&";
            node->children.push_back(std::move(left));
            node->children.push_back(parse_equality());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_equality() {
        auto left = parse_comparison();
        while (check(Token::Kind::Eq) || check(Token::Kind::Neq)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = op.text;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_comparison());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_comparison() {
        auto left = parse_term();
        while (check(Token::Kind::Lt) || check(Token::Kind::Gt)
               || check(Token::Kind::Leq) || check(Token::Kind::Geq)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = op.text;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_term());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_term() {
        auto left = parse_factor();
        while (check(Token::Kind::Plus) || check(Token::Kind::Minus)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = op.text;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_factor());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_factor() {
        auto left = parse_unary();
        while (check(Token::Kind::Star) || check(Token::Kind::Slash) || check(Token::Kind::Percent)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Binary;
            node->value = op.text;
            node->children.push_back(std::move(left));
            node->children.push_back(parse_unary());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<ASTNode> parse_unary() {
        if (check(Token::Kind::Minus) || check(Token::Kind::Not)
            || check(Token::Kind::Plus) || check(Token::Kind::Inc) || check(Token::Kind::Dec)) {
            auto op = advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Unary;
            node->value = op.text;
            node->children.push_back(parse_unary());
            return node;
        }
        return parse_call();
    }
    std::unique_ptr<ASTNode> parse_call() {
        auto node = parse_primary();
        while (match(Token::Kind::LParen)) {
            auto call = std::make_unique<ASTNode>();
            call->type = ASTNode::Type::Call;
            call->children.push_back(std::move(node));
            if (!check(Token::Kind::RParen) && !check(Token::Kind::Eof)) {
                call->children.push_back(parse_expr());
                while (match(Token::Kind::Comma)) {
                    call->children.push_back(parse_expr());
                }
            }
            match(Token::Kind::RParen);
            node = std::move(call);
        }
        return node;
    }
    std::unique_ptr<ASTNode> parse_primary() {
        auto& t = peek();
        if (match(Token::Kind::LParen)) {
            auto node = parse_expr();
            match(Token::Kind::RParen);
            return node;
        }
        if (t.kind == Token::Kind::Ident || t.kind == Token::Kind::Function
            || t.kind == Token::Kind::True || t.kind == Token::Kind::False
            || t.kind == Token::Kind::Null || t.kind == Token::Kind::This) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::Ident;
            node->value = advance().text;
            return node;
        }
        if (t.kind == Token::Kind::IntLit || t.kind == Token::Kind::FloatLit) {
            auto node = std::make_unique<ASTNode>();
            node->type = t.kind == Token::Kind::IntLit ? ASTNode::Type::IntLit : ASTNode::Type::FloatLit;
            node->value = advance().text;
            return node;
        }
        if (t.kind == Token::Kind::StringLit) {
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::StringLit;
            node->value = advance().text;
            return node;
        }
        /* 未知：返回 null 节点避免死循环 */
        /* Error token（tokenizer 对全角字符等非法输入所产）不再静默吞成 null，
           置解析错误通道，runtime 报 parse failed 而非继续执行 */
        if (t.kind == Token::Kind::Error) {
            error_ = true;
            error_msg_ = "unexpected character: '" + t.text + "'";
            advance();
            auto node = std::make_unique<ASTNode>();
            node->type = ASTNode::Type::NullLit;
            node->value = "null";
            return node;
        }
        advance();
        auto node = std::make_unique<ASTNode>();
        node->type = ASTNode::Type::NullLit;
        node->value = "null";
        return node;
    }
};

std::unique_ptr<ASTNode> parse_program(std::vector<Token> tokens, std::string* err) {
    Parser p(std::move(tokens));
    auto ast = p.parse_program();
    if (p.has_error()) {
        if (err) *err = p.error_msg();
        return nullptr;
    }
    return ast;
}

} }
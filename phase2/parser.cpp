#include "common.h"
#include <cstdio>
#include <fstream>
#include <iostream>

class Parser {
    vector<Token> t;
    size_t p = 0;

    const Token& cur() const { return t[p]; }
    const Token& peek(size_t k) const { return t[p + k < t.size() ? p + k : t.size() - 1]; }
    bool atEOF() const { return cur().type == TokenType::END_OF_FILE; }
    static bool isSym(const Token& k, const char* s) {
        return (k.type == TokenType::KEYWORD || k.type == TokenType::OPERATOR ||
                k.type == TokenType::PUNCT) && k.lexeme == s;
    }
    bool is(const char* s) const { return isSym(cur(), s); }
    [[noreturn]] void fail(const string& m) const {
        string found = atEOF() ? "end of file" : "'" + cur().lexeme + "'";
        throw CompileError{m + ", found " + found, cur().line, cur().col};
    }
    Token advance() { Token k = cur(); if (!atEOF()) p++; return k; }
    void expect(const char* s) { if (!is(s)) fail(string("expected '") + s + "'"); advance(); }
    string expectIdent() {
        if (cur().type != TokenType::IDENT) fail("expected identifier");
        return advance().lexeme;
    }
    void expectEnd() { expect("khatam"); if (is(";")) advance(); }   
    ASTNode* mk(NodeKind k, const string& text, int line) { return new ASTNode(k, text, line); }

    bool isTypeStart() const { return is("poora") || is("ashari") || is("harf") || is("dhancha"); }

    string parseType() {
        if (is("poora") || is("ashari") || is("harf")) return advance().lexeme;
        if (is("dhancha")) { advance(); return "dhancha " + expectIdent(); }
        fail("expected a type (poora, ashari, harf or dhancha)");
    }

    ASTNode* parseStruct() {
        int line = cur().line;
        expect("dhancha");
        ASTNode* s = mk(NodeKind::StructDecl, expectIdent(), line);
        expect("shuru");
        while (!is("khatam")) {
            if (atEOF()) fail("expected 'khatam' to close struct");
            int fl = cur().line;
            string ty = parseType();
            ASTNode* f = mk(NodeKind::Field, expectIdent(), fl);
            f->type = ty;
            expect(";");
            s->children.push_back(f);
        }
        expectEnd();
        return s;
    }

    ASTNode* parseVarDecl() {
        int line = cur().line;
        string ty = parseType();
        ASTNode* v = mk(NodeKind::VarDecl, expectIdent(), line);
        v->type = ty;
        if (is("[")) {
            advance();
            if (cur().type != TokenType::INT_LIT) fail("array size must be an integer literal");
            v->arraySize = stoi(advance().lexeme);
            if (v->arraySize <= 0) fail("array size must be positive");
            expect("]");
        }
        if (is("maidaalo")) { advance(); v->children.push_back(parseExpr()); }
        expect(";");
        return v;
    }

    ASTNode* parseFunction() {
        int line = cur().line;
        expect("kaam");
        string ret = is("khali") ? advance().lexeme : parseType();
        ASTNode* f = mk(NodeKind::Function, expectIdent(), line);
        f->type = ret;
        expect("(");
        if (!is(")")) {
            do {
                int pl = cur().line;
                string ty = parseType();
                ASTNode* par = mk(NodeKind::Param, expectIdent(), pl);
                par->type = ty;
                f->children.push_back(par);
                if (is(",")) advance(); else break;
            } while (true);
        }
        expect(")");
        f->children.push_back(parseBlock());
        return f;
    }

    ASTNode* parseBlock() {
        ASTNode* b = mk(NodeKind::Block, "", cur().line);
        expect("shuru");
        while (!is("khatam")) {
            if (atEOF()) fail("expected 'khatam' to close block");
            b->children.push_back(parseStatement());
        }
        expectEnd();
        return b;
    }

    ASTNode* parseStatement() {
        int line = cur().line;
        if (isTypeStart()) return parseVarDecl();
        if (is("agar")) return parseIf();
        if (is("jabtak")) {
            advance();
            ASTNode* w = mk(NodeKind::While, "", line);
            expect("("); w->children.push_back(parseExpr()); expect(")");
            w->children.push_back(parseBlock());
            return w;
        }
        if (is("wapas")) {
            advance();
            ASTNode* r = mk(NodeKind::Return, "", line);
            if (!is(";")) r->children.push_back(parseExpr());
            expect(";");
            return r;
        }
        if (is("chhaapo")) {
            advance();
            ASTNode* pr = mk(NodeKind::Print, "", line);
            expect("(");
            if (cur().type == TokenType::STRING_LIT) {
                Token s = advance();
                pr->children.push_back(mk(NodeKind::StrLit, s.lexeme, s.line));
            } else pr->children.push_back(parseExpr());
            expect(")"); expect(";");
            return pr;
        }
        if (is("nikaal")) { advance(); expect(";"); return mk(NodeKind::Break, "", line); }
        if (is("jaari"))  { advance(); expect(";"); return mk(NodeKind::Continue, "", line); }
        if (is("shuru")) return parseBlock();
        if (is("khatam") || is("warna")) fail("unexpected keyword");

        ASTNode* e = parseExpr();
        if (is("maidaalo")) {
            if (e->kind != NodeKind::Ident && e->kind != NodeKind::Index && e->kind != NodeKind::Member)
                fail("left side of 'maidaalo' must be a variable, array element or field");
            int al = cur().line;
            advance();
            ASTNode* a = mk(NodeKind::Assign, "", al);
            a->children.push_back(e);
            a->children.push_back(parseExpr());
            expect(";");
            return a;
        }
        expect(";");
        return e;
    }

    ASTNode* parseIf() {
        int line = cur().line;
        expect("agar");
        ASTNode* n = mk(NodeKind::If, "", line);
        expect("("); n->children.push_back(parseExpr()); expect(")");
        n->children.push_back(parseBlock());
        if (is("warna")) {
            advance();
            n->children.push_back(is("agar") ? parseIf() : parseBlock());
        }
        return n;
    }

    ASTNode* parseExpr() { return parseBinary(0); }

    ASTNode* parseBinary(size_t lvl) {
        static const vector<vector<string>> L = {{"||"}, {"&&"}, {"==", "!="},
            {"<", ">", "<=", ">="}, {"+", "-"}, {"*", "/", "%"}};
        if (lvl == L.size()) return parseUnary();
        ASTNode* left = parseBinary(lvl + 1);
        while (true) {
            bool match = false;
            if (cur().type == TokenType::OPERATOR)
                for (auto& op : L[lvl]) if (cur().lexeme == op) match = true;
            if (!match) break;
            Token op = advance();
            ASTNode* b = mk(NodeKind::Binary, op.lexeme, op.line);
            b->children.push_back(left);
            b->children.push_back(parseBinary(lvl + 1));
            left = b;
        }
        return left;
    }

    ASTNode* parseUnary() {
        if (is("-") || is("!")) {
            Token op = advance();
            ASTNode* u = mk(NodeKind::Unary, op.lexeme, op.line);
            u->children.push_back(parseUnary());
            return u;
        }
        return parsePostfix();
    }

    ASTNode* parsePostfix() {
        ASTNode* e = parsePrimary();
        while (true) {
            int line = cur().line;
            if (is("[")) {
                advance();
                ASTNode* ix = mk(NodeKind::Index, "", line);
                ix->children.push_back(e);
                ix->children.push_back(parseExpr());
                expect("]");
                e = ix;
            } else if (is(".")) {
                advance();
                ASTNode* m = mk(NodeKind::Member, expectIdent(), line);
                m->children.push_back(e);
                e = m;
            } else if (is("(")) {
                if (e->kind != NodeKind::Ident) fail("only a function name can be called");
                advance();
                ASTNode* c = mk(NodeKind::Call, e->text, e->line);
                delete e;
                if (!is(")")) {
                    do { c->children.push_back(parseExpr()); if (is(",")) advance(); else break; } while (true);
                }
                expect(")");
                e = c;
            } else break;
        }
        return e;
    }

    ASTNode* parsePrimary() {
        const Token& k = cur();
        switch (k.type) {
            case TokenType::INT_LIT:   { Token x = advance(); return mk(NodeKind::IntLit, x.lexeme, x.line); }
            case TokenType::FLOAT_LIT: { Token x = advance(); return mk(NodeKind::FloatLit, x.lexeme, x.line); }
            case TokenType::CHAR_LIT:  { Token x = advance(); return mk(NodeKind::CharLit, x.lexeme, x.line); }
            case TokenType::IDENT:     { Token x = advance(); return mk(NodeKind::Ident, x.lexeme, x.line); }
            default: break;
        }
        if (is("(")) { advance(); ASTNode* e = parseExpr(); expect(")"); return e; }
        fail("expected an expression");
    }

public:
    explicit Parser(vector<Token> toks) : t(move(toks)) {}

    ASTNode* parseProgram() {
        ASTNode* prog = mk(NodeKind::Program, "", 1);
        while (!atEOF()) {
            if (is("dhancha") && isSym(peek(2), "shuru")) prog->children.push_back(parseStruct());
            else if (is("kaam")) prog->children.push_back(parseFunction());
            else if (isTypeStart()) prog->children.push_back(parseVarDecl());
            else fail("expected a struct, global variable or function");
        }
        return prog;
    }
};

static void printAST(ASTNode* n, int depth, ostream& out) {
    out << string(depth * 2, ' ') << nodeKindName(n->kind);
    if (!n->text.empty()) out << " " << n->text;
    if (!n->type.empty()) out << " : " << n->type;
    if (n->arraySize > 0) out << " [" << n->arraySize << "]";
    out << "\n";
    for (auto c : n->children) printAST(c, depth + 1, out);
}

static vector<Token> readTokens(const string& path) {
    ifstream in(path);
    if (!in) throw CompileError{"cannot open " + path, 0, 0};
    vector<Token> toks;
    string ln;
    while (getline(in, ln)) {
        if (ln.empty()) continue;
        size_t a = ln.find('\t'), b = ln.find('\t', a + 1);
        if (a == string::npos || b == string::npos) throw CompileError{"malformed .tok line: " + ln, 0, 0};
        Token k;
        if (sscanf(ln.substr(0, a).c_str(), "%d:%d", &k.line, &k.col) != 2 ||
            !tokenTypeFromName(ln.substr(a + 1, b - a - 1), k.type))
            throw CompileError{"malformed .tok line: " + ln, 0, 0};
        k.lexeme = ln.substr(b + 1);
        toks.push_back(k);
    }
    if (toks.empty() || toks.back().type != TokenType::END_OF_FILE)
        throw CompileError{".tok file must end with an EOF token", 0, 0};
    return toks;
}

int main(int argc, char** argv) {
    if (argc != 2) { cerr << "usage: parser <file.tok>\n"; return 2; }
    try {
        Parser ps(readTokens(argv[1]));
        ASTNode* root = ps.parseProgram();
        string outName = replaceExt(argv[1], ".ast");
        ofstream out(outName);
        printAST(root, 0, out);
        cout << "parser: OK -> " << outName << "\n";
        delete root;
    } catch (CompileError& e) {
        cerr << argv[1] << ":" << e.line << ":" << e.col << ": syntax error: " << e.msg << "\n";
        return 1;
    }
    return 0;
}

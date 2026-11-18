#include "common.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

static const set<string> KEYWORDS = {"shuru","khatam","agar","warna","jabtak",
    "nikaal","jaari","wapas","maidaalo","chhaapo","kaam","dhancha","poora",
    "ashari","harf","khali"};

vector<Token> lex(const string& src) {
    vector<Token> out;
    size_t i = 0, n = src.size();
    int line = 1, col = 1;
    auto adv = [&]() { if (src[i] == '\n') { line++; col = 1; } else col++; i++; };
    auto err = [&](const string& m, int l, int c) { throw CompileError{m, l, c}; };

    while (i < n) {
        char c = src[i];
        if (isspace((unsigned char)c)) { adv(); continue; }
        if (c == '/' && i + 1 < n && src[i + 1] == '/') {      
            while (i < n && src[i] != '\n') adv();
            continue;
        }
        int sl = line, sc = col;
        string s;
        if (isalpha((unsigned char)c) || c == '_') {
            while (i < n && (isalnum((unsigned char)src[i]) || src[i] == '_')) { s += src[i]; adv(); }
            out.push_back({KEYWORDS.count(s) ? TokenType::KEYWORD : TokenType::IDENT, s, sl, sc});
        } else if (isdigit((unsigned char)c)) {
            while (i < n && isdigit((unsigned char)src[i])) { s += src[i]; adv(); }
            TokenType t = TokenType::INT_LIT;
            if (i + 1 < n && src[i] == '.' && isdigit((unsigned char)src[i + 1])) {
                s += '.'; adv();
                while (i < n && isdigit((unsigned char)src[i])) { s += src[i]; adv(); }
                t = TokenType::FLOAT_LIT;
            }
            out.push_back({t, s, sl, sc});
        } else if (c == '\'') {
            adv();
            if (i >= n || src[i] == '\n') err("unterminated char literal", sl, sc);
            if (src[i] == '\'') err("empty char literal", sl, sc);
            s = string("'") + src[i]; adv();
            if (i >= n || src[i] != '\'') err("unterminated char literal", sl, sc);
            adv(); s += '\'';
            out.push_back({TokenType::CHAR_LIT, s, sl, sc});
        } else if (c == '"') {
            s = "\""; adv();
            while (i < n && src[i] != '"' && src[i] != '\n') { s += src[i]; adv(); }
            if (i >= n || src[i] != '"') err("unterminated string literal", sl, sc);
            s += '"'; adv();
            out.push_back({TokenType::STRING_LIT, s, sl, sc});
        } else {
            if (i + 1 < n) {
                string two = src.substr(i, 2);
                if (two == "<=" || two == ">=" || two == "==" || two == "!=" ||
                    two == "&&" || two == "||") {
                    adv(); adv();
                    out.push_back({TokenType::OPERATOR, two, sl, sc});
                    continue;
                }
            }
            if (string("+-*/%<>!").find(c) != string::npos) {
                adv(); out.push_back({TokenType::OPERATOR, string(1, c), sl, sc});
            } else if (string("()[],;.").find(c) != string::npos) {
                adv(); out.push_back({TokenType::PUNCT, string(1, c), sl, sc});
            } else {
                err(string("unknown character '") + c + "'", sl, sc);
            }
        }
    }
    out.push_back({TokenType::END_OF_FILE, "", line, col});
    return out;
}

int main(int argc, char** argv) {
    if (argc != 2) { cerr << "usage: lexer <file.citu>\n"; return 2; }
    ifstream in(argv[1]);
    if (!in) { cerr << "cannot open " << argv[1] << "\n"; return 2; }
    stringstream ss; ss << in.rdbuf();
    try {
        vector<Token> toks = lex(ss.str());
        string outName = replaceExt(argv[1], ".tok");
        ofstream out(outName);
        for (auto& t : toks)
            out << t.line << ":" << t.col << "\t" << tokenTypeName(t.type) << "\t" << t.lexeme << "\n";
        cout << "lexer: " << toks.size() << " tokens -> " << outName << "\n";
    } catch (CompileError& e) {
        cerr << argv[1] << ":" << e.line << ":" << e.col << ": lexical error: " << e.msg << "\n";
        return 1;
    }
    return 0;
}

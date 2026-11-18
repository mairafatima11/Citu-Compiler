//shared definitions for the Citu compiler (Token, ASTNode, errors)
#pragma once
#include <string>
#include <vector>
using namespace std;

enum class TokenType { KEYWORD, IDENT, INT_LIT, FLOAT_LIT, CHAR_LIT,
                       STRING_LIT, OPERATOR, PUNCT, END_OF_FILE };

inline const char* tokenTypeName(TokenType t) {
    static const char* n[] = {"KEYWORD","IDENT","INT_LIT","FLOAT_LIT","CHAR_LIT",
                              "STRING_LIT","OPERATOR","PUNCT","EOF"};
    return n[(int)t];
}
inline bool tokenTypeFromName(const string& s, TokenType& out) {
    for (int i = 0; i <= (int)TokenType::END_OF_FILE; i++)
        if (s == tokenTypeName((TokenType)i)) { out = (TokenType)i; return true; }
    return false;
}

struct Token {
    TokenType type;
    string lexeme;
    int line;
    int col;
};

struct CompileError {
    string msg;
    int line, col;
};

enum class NodeKind { Program, StructDecl, Field, Function, Param, Block,
    VarDecl, Assign, If, While, Break, Continue, Return, Print, Call,
    Binary, Unary, Index, Member, IntLit, FloatLit, CharLit, StrLit, Ident };

inline const char* nodeKindName(NodeKind k) {
    static const char* n[] = {"Program","StructDecl","Field","Function","Param",
        "Block","VarDecl","Assign","If","While","Break","Continue","Return",
        "Print","Call","Binary","Unary","Index","Member","IntLit","FloatLit",
        "CharLit","StrLit","Ident"};
    return n[(int)k];
}

struct ASTNode {
    NodeKind kind;
    string text;      
    string type;      
    int arraySize = 0;
    int line = 0;
    vector<ASTNode*> children;
    ASTNode(NodeKind k, string t, int l) : kind(k), text(move(t)), line(l) {}
    ~ASTNode() { for (auto c : children) delete c; }
};

// "dir/name.citu" + ".tok" -> "dir/name.tok"
inline string replaceExt(const string& path, const string& ext) {
    size_t dot = path.find_last_of('.'), slash = path.find_last_of("/\\");
    if (dot == string::npos || (slash != string::npos && dot < slash)) return path + ext;
    return path.substr(0, dot) + ext;
}

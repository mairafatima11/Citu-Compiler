// ast_io.h - write/read the indented .ast text format shared by parser and semantic analyzer.
// Line format:  <2*depth spaces>Kind [text] [: type] [[N]] [@line]
#pragma once
#include "common.h"
#include <cctype>
#include <fstream>
#include <iostream>

inline void printAST(ASTNode* n, int depth, ostream& out) {
    out << string(depth * 2, ' ') << nodeKindName(n->kind);
    if (!n->text.empty()) out << " " << n->text;
    if (!n->type.empty()) out << " : " << n->type;
    if (n->arraySize > 0) out << " [" << n->arraySize << "]";
    if (n->line > 0) out << " @" << n->line;
    out << "\n";
    for (auto c : n->children) printAST(c, depth + 1, out);
}

inline bool nodeKindFromName(const string& s, NodeKind& out) {
    for (int i = 0; i <= (int)NodeKind::Ident; i++)
        if (s == nodeKindName((NodeKind)i)) { out = (NodeKind)i; return true; }
    return false;
}

inline ASTNode* readAST(const string& path) {
    ifstream in(path);
    if (!in) throw CompileError{"cannot open " + path, 0, 0};
    vector<pair<int, ASTNode*>> stack;   // (depth, node)
    ASTNode* root = nullptr;
    string ln;
    int lineNo = 0;
    auto bad = [&]() { throw CompileError{"malformed .ast at line " + to_string(lineNo) + ": " + ln, 0, 0}; };

    while (getline(in, ln)) {
        lineNo++;
        if (ln.empty()) continue;
        size_t sp = 0;
        while (sp < ln.size() && ln[sp] == ' ') sp++;
        if (sp % 2) bad();
        int depth = (int)sp / 2;
        string body = ln.substr(sp);

        int srcLine = 0;                                   // optional trailing " @N"
        size_t at = body.rfind(" @");
        if (at != string::npos && at + 2 < body.size()) {
            string d = body.substr(at + 2);
            bool digits = true;
            for (char c : d) if (!isdigit((unsigned char)c)) digits = false;
            if (digits) { srcLine = stoi(d); body = body.substr(0, at); }
        }
        size_t k = body.find(' ');
        string kindName = body.substr(0, k);
        string rest = (k == string::npos) ? "" : body.substr(k + 1);
        NodeKind kind;
        if (!nodeKindFromName(kindName, kind)) bad();

        string text, type;
        int arr = 0;
        auto tailType = [&](const string& tail) { if (tail.compare(0, 3, " : ") == 0) type = tail.substr(3); };
        if (kind == NodeKind::StrLit) {                    // text may contain anything
            size_t q = rest.rfind('"');
            if (rest.empty() || rest[0] != '"' || q == 0) bad();
            text = rest.substr(0, q + 1);
            tailType(rest.substr(q + 1));
        } else if (kind == NodeKind::CharLit) {
            if (rest.size() < 3) bad();
            text = rest.substr(0, 3);
            tailType(rest.substr(3));
        } else {
            if (kind == NodeKind::VarDecl && !rest.empty() && rest.back() == ']') {
                size_t b = rest.rfind(" [");
                if (b == string::npos) bad();
                arr = stoi(rest.substr(b + 2));
                rest = rest.substr(0, b);
            }
            string s = " " + rest;
            size_t pos = s.find(" : ");
            if (pos != string::npos) { text = s.substr(1, pos > 0 ? pos - 1 : 0); type = s.substr(pos + 3); }
            else text = rest;
        }
        ASTNode* node = new ASTNode(kind, text, srcLine);
        node->type = type;
        node->arraySize = arr;
        while (!stack.empty() && stack.back().first >= depth) stack.pop_back();
        if (stack.empty()) { if (root) bad(); root = node; }
        else stack.back().second->children.push_back(node);
        stack.push_back({depth, node});
    }
    if (!root) throw CompileError{"empty .ast file", 0, 0};
    return root;
}

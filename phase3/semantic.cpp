// semantic.cpp - Citu semantic analyzer.
// Usage: semantic prog.ast  -> writes prog.sym and rewrites prog.ast with types filled in.
#include "common.h"
#include "ast_io.h"
#include <algorithm>
#include <deque>
#include <unordered_map>

enum class SymKind { VAR, FUNC, PARAM, STRUCT };
static const char* symKindName(SymKind k) {
    static const char* n[] = {"VAR", "FUNC", "PARAM", "STRUCT"};
    return n[(int)k];
}

struct Field { string name, type; };

struct Symbol {
    string name;
    SymKind kind = SymKind::VAR;
    string type;                 // "poora", "ashari", "harf", "dhancha Nuqta", "khali"
    int arraySize = 0;           // 0 if not an array
    int scopeLevel = 0;
    int line = 0;                // declaration line
    vector<string> paramTypes;   // FUNC only
};

struct Scope {
    int level = 0;
    unordered_map<string, Symbol> map;
    vector<string> order;        // declaration order (for the dump)
};

static const string ERR = "<error>";   // type of an expression that already produced an error

static bool isStruct(const string& t) { return t.compare(0, 8, "dhancha ") == 0; }
static bool isNumeric(const string& t) { return t == "poora" || t == "ashari" || t == "harf"; }
static bool isIntegral(const string& t) { return t == "poora" || t == "harf"; }

class Analyzer {
    deque<Scope> all;                 // every scope ever created, in creation order
    vector<Scope*> stack;             // currently open scopes
    unordered_map<string, vector<Field>> structTable;
    string curRet;
    int loopDepth = 0;

public:
    struct Err { int line; string msg; };
    vector<Err> errors;

    void error(int line, const string& m) { errors.push_back({line, m}); }

    // ---- scopes ----
    void push() { all.emplace_back(); all.back().level = (int)stack.size(); stack.push_back(&all.back()); }
    void pop() { stack.pop_back(); }
    Symbol* lookup(const string& name) {
        for (int i = (int)stack.size() - 1; i >= 0; i--) {
            auto it = stack[i]->map.find(name);
            if (it != stack[i]->map.end()) return &it->second;
        }
        return nullptr;
    }
    bool declare(Scope* sc, Symbol s, int line) {
        auto it = sc->map.find(s.name);
        if (it != sc->map.end()) {
            error(line, "redeclaration of '" + s.name + "' (first declared on line " + to_string(it->second.line) + ")");
            return false;
        }
        s.scopeLevel = sc->level;
        s.line = line;
        sc->order.push_back(s.name);
        sc->map[s.name] = s;
        return true;
    }

    // ---- type helpers ----
    bool validType(const string& t, int line) {
        if (isStruct(t) && !structTable.count(t.substr(8))) {
            error(line, "unknown struct '" + t.substr(8) + "'");
            return false;
        }
        return true;
    }
    static bool assignable(const string& dst, const string& src) {
        if (dst == ERR || src == ERR) return true;
        if (src == "khali" || dst == "khali") return false;
        if (dst == src) return true;
        if (dst == "ashari") return src == "poora" || src == "harf";
        if (dst == "poora") return src == "harf";
        if (dst == "harf") return src == "poora";
        return false;                                  // includes ashari -> poora (narrowing)
    }
    static bool constInt(ASTNode* n, long& v) {
        if (n->kind == NodeKind::IntLit) { try { v = stol(n->text); return true; } catch (...) { return false; } }
        if (n->kind == NodeKind::Unary && n->text == "-" && constInt(n->children[0], v)) { v = -v; return true; }
        return false;
    }
    static bool isConstExpr(ASTNode* n) {
        switch (n->kind) {
            case NodeKind::IntLit: case NodeKind::FloatLit: case NodeKind::CharLit: return true;
            case NodeKind::Unary: return isConstExpr(n->children[0]);
            case NodeKind::Binary: return isConstExpr(n->children[0]) && isConstExpr(n->children[1]);
            default: return false;
        }
    }

    // ---- expressions: return the type (or ERR) and annotate the node ----
    Symbol* useIdent(ASTNode* id, bool allowArray) {
        Symbol* s = lookup(id->text);
        if (!s) { error(id->line, "undeclared identifier '" + id->text + "'"); return nullptr; }
        if (s->kind == SymKind::FUNC)   { error(id->line, "'" + id->text + "' is a function, not a variable"); return nullptr; }
        if (s->kind == SymKind::STRUCT) { error(id->line, "'" + id->text + "' is a struct name, not a variable"); return nullptr; }
        if (s->arraySize > 0 && !allowArray) { error(id->line, "array '" + id->text + "' must be indexed"); return nullptr; }
        id->type = s->type;
        return s;
    }

    string expr(ASTNode* n) {
        string r = exprImpl(n);
        if (r != ERR) n->type = r;
        return r;
    }

    string exprImpl(ASTNode* n) {
        switch (n->kind) {
            case NodeKind::IntLit:   return "poora";
            case NodeKind::FloatLit: return "ashari";
            case NodeKind::CharLit:  return "harf";
            case NodeKind::StrLit:
                error(n->line, "string literals are only allowed inside chhaapo");
                return ERR;
            case NodeKind::Ident: {
                Symbol* s = useIdent(n, false);
                return s ? s->type : ERR;
            }
            case NodeKind::Unary: {
                string t = expr(n->children[0]);
                if (t == ERR) return ERR;
                if (!isNumeric(t)) {
                    error(n->line, "operator '" + n->text + "' cannot be applied to " + t);
                    return ERR;
                }
                return (n->text == "-" && t == "harf") ? "poora" : (n->text == "!" ? "poora" : t);
            }
            case NodeKind::Binary: return binary(n);
            case NodeKind::Call:   return call(n);
            case NodeKind::Index: {
                ASTNode* base = n->children[0];
                string it = expr(n->children[1]);
                if (base->kind != NodeKind::Ident) {
                    error(n->line, "only array variables can be indexed");
                    return ERR;
                }
                Symbol* s = useIdent(base, true);
                if (!s) return ERR;
                if (s->arraySize == 0) { error(n->line, "'" + s->name + "' is not an array"); return ERR; }
                if (it != ERR && it != "poora") {
                    error(n->line, "array index must be poora, found " + it);
                    return ERR;
                }
                long v;
                if (constInt(n->children[1], v) && (v < 0 || v >= s->arraySize)) {
                    error(n->line, "index " + to_string(v) + " is out of bounds for array '" + s->name +
                                   "' of size " + to_string(s->arraySize));
                    return ERR;
                }
                return s->type;
            }
            case NodeKind::Member: {
                string bt = expr(n->children[0]);
                if (bt == ERR) return ERR;
                if (!isStruct(bt)) { error(n->line, "'.' requires a struct, found " + bt); return ERR; }
                auto& fields = structTable[bt.substr(8)];
                for (auto& f : fields) if (f.name == n->text) return f.type;
                error(n->line, "struct '" + bt.substr(8) + "' has no field '" + n->text + "'");
                return ERR;
            }
            default:
                error(n->line, string("unexpected ") + nodeKindName(n->kind) + " in expression");
                return ERR;
        }
    }

    string binary(ASTNode* n) {
        string l = expr(n->children[0]), r = expr(n->children[1]);
        if (l == ERR || r == ERR) return ERR;
        const string& op = n->text;
        auto bad = [&]() {
            error(n->line, "operator '" + op + "' cannot be applied to " + l + " and " + r);
            return ERR;
        };
        if ((op == "/" || op == "%") && n->children[1]->kind == NodeKind::IntLit && n->children[1]->text == "0") {
            error(n->line, "division by zero");
            return ERR;
        }
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            if (!isNumeric(l) || !isNumeric(r)) return bad();
            return (l == "ashari" || r == "ashari") ? "ashari" : "poora";
        }
        if (op == "%") {
            if (!isIntegral(l) || !isIntegral(r)) return bad();
            return "poora";
        }
        // comparison and logical operators
        if (!isNumeric(l) || !isNumeric(r)) return bad();
        return "poora";
    }

    string call(ASTNode* n) {
        vector<string> at;
        for (auto a : n->children) at.push_back(expr(a));
        Symbol* s = lookup(n->text);
        if (!s) { error(n->line, "undeclared function '" + n->text + "'"); return ERR; }
        if (s->kind != SymKind::FUNC) { error(n->line, "'" + n->text + "' is not a function"); return ERR; }
        if (at.size() != s->paramTypes.size()) {
            error(n->line, "function '" + n->text + "' expects " + to_string(s->paramTypes.size()) +
                           " argument(s), got " + to_string(at.size()));
            return s->type;
        }
        for (size_t i = 0; i < at.size(); i++)
            if (!assignable(s->paramTypes[i], at[i]))
                error(n->children[i]->line ? n->children[i]->line : n->line,
                      "argument " + to_string(i + 1) + " of '" + n->text + "': expected " +
                      s->paramTypes[i] + ", got " + at[i]);
        return s->type;
    }

    void checkCondition(ASTNode* c) {
        string t = expr(c);
        if (t != ERR && !isNumeric(t))
            error(c->line, "condition must be a number (poora, ashari or harf), found " + t);
    }

    // ---- declarations and statements ----
    void declareVar(ASTNode* n, bool global) {
        bool ok = validType(n->type, n->line);
        if (!n->children.empty()) {
            ASTNode* init = n->children[0];
            string it = expr(init);
            if (n->arraySize > 0) error(n->line, "array '" + n->text + "' cannot have an initializer");
            else if (ok && !assignable(n->type, it))
                error(n->line, "cannot initialize '" + n->text + "' of type " + n->type + " with a value of type " + it);
            if (global && !isConstExpr(init))
                error(n->line, "initializer of global variable '" + n->text + "' must be a constant expression");
        }
        Symbol s;
        s.name = n->text; s.kind = SymKind::VAR; s.type = ok ? n->type : ERR; s.arraySize = n->arraySize;
        declare(stack.back(), s, n->line);
    }

    void declareStruct(ASTNode* n) {
        Symbol s; s.name = n->text; s.kind = SymKind::STRUCT;
        bool fresh = declare(stack[0], s, n->line);
        vector<Field> fields;
        for (auto f : n->children) {
            validType(f->type, f->line);
            for (auto& g : fields)
                if (g.name == f->text) error(f->line, "duplicate field '" + f->text + "' in struct '" + n->text + "'");
            fields.push_back({f->text, f->type});
        }
        if (fresh) structTable[n->text] = fields;
    }

    void declareFunction(ASTNode* n) {
        Symbol f; f.name = n->text; f.kind = SymKind::FUNC; f.type = n->type;
        if (n->type != "khali") validType(n->type, n->line);
        for (size_t i = 0; i + 1 < n->children.size(); i++) {
            validType(n->children[i]->type, n->children[i]->line);
            f.paramTypes.push_back(n->children[i]->type);
        }
        declare(stack[0], f, n->line);          // visible inside its own body (recursion)

        push();                                 // function scope: parameters + body
        for (size_t i = 0; i + 1 < n->children.size(); i++) {
            ASTNode* p = n->children[i];
            Symbol ps; ps.name = p->text; ps.kind = SymKind::PARAM; ps.type = p->type;
            declare(stack.back(), ps, p->line);
        }
        curRet = n->type;
        loopDepth = 0;
        block(n->children.back(), false);       // body shares the parameter scope
        pop();
    }

    void block(ASTNode* b, bool newScope = true) {
        if (newScope) push();
        for (auto s : b->children) stmt(s);
        if (newScope) pop();
    }

    void stmt(ASTNode* n) {
        switch (n->kind) {
            case NodeKind::VarDecl: declareVar(n, false); break;
            case NodeKind::Assign: {
                string lt = expr(n->children[0]), rt = expr(n->children[1]);
                if (lt != ERR && rt != ERR && !assignable(lt, rt))
                    error(n->line, "cannot assign a value of type " + rt + " to a " + lt);
                break;
            }
            case NodeKind::If:
                checkCondition(n->children[0]);
                block(n->children[1]);
                if (n->children.size() > 2) {
                    if (n->children[2]->kind == NodeKind::If) stmt(n->children[2]);
                    else block(n->children[2]);
                }
                break;
            case NodeKind::While:
                checkCondition(n->children[0]);
                loopDepth++;
                block(n->children[1]);
                loopDepth--;
                break;
            case NodeKind::Break:
                if (loopDepth == 0) error(n->line, "'nikaal' used outside of a loop");
                break;
            case NodeKind::Continue:
                if (loopDepth == 0) error(n->line, "'jaari' used outside of a loop");
                break;
            case NodeKind::Return: {
                bool has = !n->children.empty();
                string t = has ? expr(n->children[0]) : "";
                if (curRet == "khali") {
                    if (has) error(n->line, "function returns khali, so 'wapas' cannot return a value");
                } else if (!has) {
                    error(n->line, "'wapas' needs a value of type " + curRet);
                } else if (!assignable(curRet, t)) {
                    error(n->line, "cannot return a value of type " + t + " from a function returning " + curRet);
                }
                break;
            }
            case NodeKind::Print: {
                ASTNode* a = n->children[0];
                if (a->kind == NodeKind::StrLit) break;
                string t = expr(a);
                if (t != ERR && !isNumeric(t)) error(n->line, "cannot print a value of type " + t);
                break;
            }
            case NodeKind::Block: block(n); break;
            default: expr(n); break;            // expression statement (e.g. a call)
        }
    }

    void run(ASTNode* root) {
        push();                                  // scope 0: global
        for (auto c : root->children) {
            if (c->kind == NodeKind::StructDecl) declareStruct(c);
            else if (c->kind == NodeKind::VarDecl) declareVar(c, true);
            else if (c->kind == NodeKind::Function) declareFunction(c);
        }
        Symbol* m = stack[0]->map.count("asal") ? &stack[0]->map["asal"] : nullptr;
        if (!m || m->kind != SymKind::FUNC) error(0, "program must define a function named 'asal'");
        else if (!m->paramTypes.empty()) error(m->line, "function 'asal' must take no parameters");
        pop();
    }

    // ---- symbol table dump:  scope \t name \t kind \t type \t extra ----
    void dump(ostream& out) {
        for (auto& sc : all)                     // scope 0 is first, others in creation order
            for (auto& name : sc.order) {
                const Symbol& s = sc.map.at(name);
                string extra = "-";
                if (s.kind == SymKind::FUNC) {
                    extra = "params=(";
                    for (size_t i = 0; i < s.paramTypes.size(); i++) extra += (i ? "," : "") + s.paramTypes[i];
                    extra += ")";
                } else if (s.kind == SymKind::STRUCT) {
                    extra = "fields=(";
                    auto& fs = structTable.at(s.name);
                    for (size_t i = 0; i < fs.size(); i++) extra += (i ? "," : "") + fs[i].name + ":" + fs[i].type;
                    extra += ")";
                } else if (s.arraySize > 0) extra = "size=" + to_string(s.arraySize);
                out << s.scopeLevel << "\t" << s.name << "\t" << symKindName(s.kind) << "\t"
                    << (s.kind == SymKind::STRUCT ? "-" : s.type) << "\t" << extra << "\n";
            }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) { cerr << "usage: semantic <file.ast>\n"; return 2; }
    string path = argv[1];
    try {
        ASTNode* root = readAST(path);
        Analyzer an;
        an.run(root);
        if (!an.errors.empty()) {
            stable_sort(an.errors.begin(), an.errors.end(),
                        [](const Analyzer::Err& a, const Analyzer::Err& b) { return a.line < b.line; });
            for (auto& e : an.errors) {
                if (e.line > 0) cerr << path << ":" << e.line << ": semantic error: " << e.msg << "\n";
                else cerr << path << ": semantic error: " << e.msg << "\n";
            }
            cerr << an.errors.size() << " semantic error(s)\n";
            return 1;
        }
        string symName = replaceExt(path, ".sym");
        ofstream sym(symName);
        an.dump(sym);
        sym.close();
        ofstream ast(path);                      // rewrite the AST with types added
        printAST(root, 0, ast);
        cout << "semantic: OK -> " << symName << " (and typed " << path << ")\n";
        delete root;
    } catch (CompileError& e) {
        cerr << path << ": error: " << e.msg << "\n";
        return 1;
    }
    return 0;
}

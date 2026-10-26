/* Expressões compiladas: nomes viram posições; NULL segue a lógica do SQL. */
#include "tatu/internal.h"
#include "lib/utf8.h"

struct cexpr {
    const node_t *node;
    int position;                       /* >= 0: valor já calculado na linha */
    struct cexpr *a, *b, *c, **items;
};

scope_t scope_concat(scope_t a, scope_t b) {
    scope_t out = { .n = a.n + b.n };
    out.slots = ar_alloc(TMP, (size_t)out.n * sizeof(slot_t));
    memcpy(out.slots, a.slots, (size_t)a.n * sizeof(slot_t));
    memcpy(out.slots + a.n, b.slots, (size_t)b.n * sizeof(slot_t));
    return out;
}

int scope_find_expr(const scope_t *s, const node_t *e) {
    for (int i = 0; i < s->n; i++)
        if (s->slots[i].expr && node_eq(s->slots[i].expr, e)) return i;
    return -1;
}

int scope_try_resolve(const scope_t *s, const node_t *e) {
    int found = -1;
    for (int i = 0; i < s->n; i++) {
        slot_t *p = &s->slots[i];
        if (!p->name || strcmp(p->name, e->name)) continue;
        if (e->table && (!p->table || strcmp(p->table, e->table))) continue;
        if (found >= 0) return -1;        /* ambiguidade também impede usar índice */
        found = i;
    }
    return found;
}

int scope_resolve(const scope_t *s, const node_t *e) {
    int found = -1;
    bool table_found = !e->table;
    for (int i = 0; i < s->n; i++) {
        slot_t *p = &s->slots[i];
        if (e->table && p->table && !strcmp(e->table, p->table)) table_found = true;
        if (!p->name || strcmp(p->name, e->name)) continue;
        if (e->table && (!p->table || strcmp(p->table, e->table))) continue;
        if (found >= 0)
            tatu_fail(TE_CATALOG, "A coluna '%s' é ambígua; diga de qual tabela ela vem", e->name);
        found = i;
    }
    if (found >= 0) return found;
    if (s->aggregated)
        tatu_fail(TE_CATALOG, "A coluna '%s' precisa estar no GROUP BY ou dentro de uma agregação", node_text(e));
    if (!table_found) tatu_fail(TE_CATALOG, "Tabela ou apelido desconhecido: '%s'", e->table);
    tatu_fail(TE_CATALOG, "Coluna desconhecida: '%s'", node_text(e));
}

int *scope_star(const scope_t *s, const char *table, int *count) {
    int *out = ar_alloc(TMP, (size_t)s->n * sizeof(int));
    *count = 0;
    for (int i = 0; i < s->n; i++) {
        slot_t *p = &s->slots[i];
        if (p->name && (!table || (p->table && !strcmp(table, p->table)))) out[(*count)++] = i;
    }
    if (table && !*count) tatu_fail(TE_CATALOG, "Tabela ou apelido desconhecido: '%s'", table);
    return out;
}

int truth(tatu_value_t v) {
    switch (v.type) {
    case TV_NULL: return -1;
    case TV_BOOL: return v.b;
    case TV_INT: return v.i != 0;
    case TV_REAL: return v.r != 0;
    default: return v.s[0] != 0;
    }
}

static tatu_value_t logic(int op, tatu_value_t a, tatu_value_t b) {
    int x = truth(a), y = truth(b);
    if (op == OP_AND) {
        if (x == 0 || y == 0) return vbool(false);
        return x < 0 || y < 0 ? vnull() : vbool(true);
    }
    if (x == 1 || y == 1) return vbool(true);
    return x < 0 || y < 0 ? vnull() : vbool(false);
}

static tatu_value_t negate_truth(tatu_value_t v) {
    int t = truth(v);
    return t < 0 ? vnull() : vbool(!t);
}

static void numbers(const char *op, tatu_value_t v) {
    if (family(v) != FAM_NUMBER)
        tatu_fail(TE_TYPE, "'%s' espera números, mas recebeu %s (%s)", op, type_of(v), sql_literal(v));
}

static tatu_value_t checked_real(double x) {
    if (x != x || x > 1.7976931348623157e308 || x < -1.7976931348623157e308)
        tatu_fail(TE_EXECUTION, "O resultado está fora do intervalo dos números REAL");
    return vreal(x);
}

/* Resto real sem libm: subtrai múltiplos em potências de dois. */
static double real_mod(double a, double b) {
    bool neg = a < 0;
    if (neg) a = -a;
    if (b < 0) b = -b;
    while (a >= b) {
        double m = b;
        while (m <= a / 2) m *= 2;
        a -= m;
    }
    return neg ? -a : a;
}

static tatu_value_t binary(int op, tatu_value_t a, tatu_value_t b) {
    if (op == OP_AND || op == OP_OR) return logic(op, a, b);
    if (a.type == TV_NULL || b.type == TV_NULL) return vnull();
    if (op >= OP_EQ && op <= OP_GE) {
        if (family(a) != family(b))
            tatu_fail(TE_TYPE, "Não dá para comparar %s com %s (%s %s %s)",
                      type_of(a), type_of(b), sql_literal(a), op_text[op], sql_literal(b));
        int c = value_cmp(a, b);
        return vbool(op == OP_EQ ? c == 0 : op == OP_NE ? c != 0 : op == OP_LT ? c < 0
                   : op == OP_LE ? c <= 0 : op == OP_GT ? c > 0 : c >= 0);
    }
    if (op == OP_CONCAT) {
        sb_t s;
        sb_init(&s, TMP);
        sb_add(&s, a.type == TV_TEXT ? a.s : sql_literal(a));
        sb_add(&s, b.type == TV_TEXT ? b.s : sql_literal(b));
        return vtext(sb_str(&s));
    }
    numbers(op_text[op], a);
    numbers(op_text[op], b);
    if ((op == OP_DIV || op == OP_MOD) && as_double(b) == 0)
        tatu_fail(TE_EXECUTION, "Divisão por zero");
    if (a.type == TV_INT && b.type == TV_INT) {
        int64_t n = 0;
        bool overflow = false;
        switch (op) {
        case OP_ADD: overflow = __builtin_add_overflow(a.i, b.i, &n); break;
        case OP_SUB: overflow = __builtin_sub_overflow(a.i, b.i, &n); break;
        case OP_MUL: overflow = __builtin_mul_overflow(a.i, b.i, &n); break;
        case OP_DIV:
            overflow = a.i == INT64_MIN && b.i == -1;
            if (!overflow) n = a.i / b.i;
            break;
        case OP_MOD: n = a.i == INT64_MIN && b.i == -1 ? 0 : a.i % b.i; break;
        }
        if (overflow) tatu_fail(TE_EXECUTION, "O resultado está fora do intervalo INTEGER de 64 bits");
        return vint(n);
    }
    double x = as_double(a), y = as_double(b);
    return checked_real(op == OP_ADD ? x + y : op == OP_SUB ? x - y : op == OP_MUL ? x * y
                      : op == OP_DIV ? x / y : real_mod(x, y));
}

bool is_aggregate(const node_t *n) {
    return n && n->kind == N_CALL && (!strcmp(n->name, "COUNT") || !strcmp(n->name, "SUM")
        || !strcmp(n->name, "AVG") || !strcmp(n->name, "MIN") || !strcmp(n->name, "MAX"));
}

static bool scalar(const char *name, int *low, int *high) {
    static const char *const unary[] = { "UPPER", "LOWER", "LENGTH", "TRIM", "ABS", "TYPEOF" };
    *low = *high = 1;
    for (unsigned i = 0; i < sizeof(unary) / sizeof(unary[0]); i++) if (!strcmp(name, unary[i])) return true;
    if (!strcmp(name, "ROUND")) { *high = 2; return true; }
    if (!strcmp(name, "COALESCE")) { *high = 0x7fffffff; return true; }
    return false;
}

static cexpr_t *compile(const node_t *n, const scope_t *s, int depth) {
    if (depth > 64) tatu_fail(TE_EXECUTION, "Expressão profunda demais; divida o cálculo em etapas");
    cexpr_t *e = ar_alloc(TMP, sizeof(*e));
    e->node = n;
    e->position = scope_find_expr(s, n);
    if (e->position >= 0) return e;
    switch (n->kind) {
    case N_LITERAL: return e;
    case N_COLUMN: e->position = scope_resolve(s, n); return e;
    case N_PARAM: tatu_fail(TE_EXECUTION, "Há um '?' sem valor; passe os parâmetros no execute()");
    case N_STAR: tatu_fail(TE_EXECUTION, "'*' só pode ser usado direto no SELECT ou em COUNT(*)");
    case N_CALL: {
        if (is_aggregate(n))
            tatu_fail(TE_EXECUTION, "%s não pode ser usado aqui; para filtrar grupos, use HAVING", node_text(n));
        int low, high;
        if (!scalar(n->name, &low, &high)) tatu_fail(TE_CATALOG, "Função desconhecida: %s()", n->name);
        if (n->star || n->distinct || n->nitems < low || n->nitems > high)
            tatu_fail(TE_EXECUTION, "Quantidade de argumentos inválida em %s()", n->name);
        break;
    }
    default: break;
    }
    if (n->a) e->a = compile(n->a, s, depth + 1);
    if (n->b) e->b = compile(n->b, s, depth + 1);
    if (n->c) e->c = compile(n->c, s, depth + 1);
    e->items = ar_alloc(TMP, (size_t)n->nitems * sizeof(cexpr_t *));
    for (int i = 0; i < n->nitems; i++) e->items[i] = compile(n->items[i], s, depth + 1);
    return e;
}

cexpr_t *compile_expr(const node_t *n, const scope_t *s) { return compile(n, s, 0); }

/* LIKE percorre caracteres UTF-8; guardar o último '%' evita recursão exponencial. */
static bool match_like(const char *s, const char *p) {
    const char *star = NULL, *retry = NULL;
    while (*s) {
        if (*p == '%') { star = ++p; retry = s; continue; }
        const char *sn = s, *pn = p;
        uint32_t sc = utf8_decode(&sn);
        uint32_t pc = *p ? utf8_decode(&pn) : 0;
        if (pc == '_' || sc == pc) { s = sn; p = pn; continue; }
        if (!star || !*retry) return false;
        utf8_decode(&retry);
        s = retry;
        p = star;
    }
    while (*p == '%') p++;
    return !*p;
}

static bool unicode_space(uint32_t cp) {
    return (cp >= 9 && cp <= 13) || (cp >= 0x1c && cp <= 0x20) || cp == 0x85 || cp == 0xa0
        || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029
        || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

/* A fonte do SO cobre Latin-1. Preservamos os demais caracteres sem corromper UTF-8. */
static tatu_value_t text_function(const char *name, tatu_value_t v) {
    if (v.type != TV_TEXT) tatu_fail(TE_TYPE, "%s() espera TEXT, mas recebeu %s", name, type_of(v));
    if (!strcmp(name, "LENGTH")) return vint(utf8_len(v.s));
    if (!strcmp(name, "TRIM")) {
        const char *begin = v.s, *p = v.s, *end = v.s;
        bool leading = true;
        while (*p) {
            const char *before = p;
            uint32_t cp = utf8_decode(&p);
            if (!unicode_space(cp)) { if (leading) begin = before; leading = false; end = p; }
        }
        return vtext(ar_strndup(TMP, begin, leading ? 0 : (size_t)(end - begin)));
    }
    bool upper = !strcmp(name, "UPPER");
    sb_t b;
    sb_init(&b, TMP);
    for (const char *p = v.s; *p;) {
        uint32_t cp = utf8_decode(&p);
        if (upper && cp == 0xdf) { sb_add(&b, "SS"); continue; }
        if (upper && ((cp >= 'a' && cp <= 'z') || (cp >= 0xe0 && cp <= 0xfe && cp != 0xf7))) cp -= 32;
        else if (!upper && ((cp >= 'A' && cp <= 'Z') || (cp >= 0xc0 && cp <= 0xde && cp != 0xd7))) cp += 32;
        if (upper && cp == 0xff) cp = 0x178;
        else if (!upper && cp == 0x178) cp = 0xff;
        char encoded[4];
        int bytes = utf8_encode(cp, encoded);
        sb_addn(&b, encoded, (size_t)bytes);
    }
    return vtext(sb_str(&b));
}

static uint64_t divide128(__uint128_t numerator, __uint128_t denominator, __uint128_t *remainder) {
    __uint128_t rem = 0, q = 0;
    for (int bit = 127; bit >= 0; bit--) {
        rem = (rem << 1) | ((numerator >> bit) & 1);
        if (rem >= denominator) { rem -= denominator; q |= (__uint128_t)1 << bit; }
    }
    *remainder = rem;
    return (uint64_t)q;
}

/* Preserva o valor binário de entrada antes de decidir o empate decimal.
 * Multiplicar 2.675 por 100 em double já arredondaria para 267.5, mas o
 * número de entrada está um pouco abaixo: ROUND(2.675,2) precisa dar 2.67. */
static double round_decimal(double x, int places, double scale) {
    bool neg = x < 0;
    double magnitude = neg ? -x : x;
    int k = places < 0 ? -places : places;
    if (k <= 22 || (places >= 0 && k <= 32)) {
        uint64_t bits;
        memcpy(&bits, &magnitude, sizeof(bits));
        unsigned exponent = (unsigned)(bits >> 52) & 0x7ff;
        uint64_t mantissa = bits & 0xfffffffffffffULL;
        if (exponent) mantissa |= 1ULL << 52;
        int power = exponent ? (int)exponent - 1075 : -1074;
        __uint128_t five = 1;
        for (int i = 0; i < k; i++) five *= 5;
        uint64_t q;
        __uint128_t remainder, denominator;
        if (places >= 0) {
            __uint128_t numerator = (__uint128_t)mantissa * five;
            int shift = power + k;
            if (shift >= 0) return x;
            if (shift < -127) {
                double tiny = shift == -128 && numerator > ((__uint128_t)1 << 127) ? 1 / scale : 0;
                return neg ? -tiny : tiny;
            }
            denominator = (__uint128_t)1 << -shift;
            __uint128_t whole = numerator >> -shift;
            if (whole > (1ULL << 53)) return x;  /* o passo decimal já é menor que um ULP */
            q = (uint64_t)whole;
            remainder = numerator & (denominator - 1);
        } else {
            if (magnitude / scale > 9007199254740992.0) return x;
            int shift = power - k;
            if (shift < -54) return neg ? -0.0 : 0.0;
            __uint128_t numerator = mantissa;
            denominator = five;
            if (shift >= 0) numerator <<= shift;
            else denominator <<= -shift;
            q = divide128(numerator, denominator, &remainder);
        }
        __uint128_t twice = remainder * 2;
        if (twice > denominator || (twice == denominator && (q & 1))) q++;
        double rounded = places < 0 ? (double)q * scale : (double)q / scale;
        return neg ? -rounded : rounded;
    }
    /* Fora das 22 casas, a precisão estendida evita o arredondamento duplo
     * das multiplicações usuais e conserva valores muito pequenos. */
    long double y = places < 0 ? (long double)x / scale : (long double)x * scale;
    if (y >= 9.22e18L || y <= -9.22e18L) return x;
    bool negative = y < 0;
    if (negative) y = -y;
    int64_t whole = (int64_t)y;
    long double fraction = y - whole;
    if (fraction > 0.5L || (fraction == 0.5L && (whole & 1))) whole++;
    y = negative ? -(long double)whole : whole;
    return places < 0 ? (double)(y * scale) : (double)(y / scale);
}

static tatu_value_t scalar_eval(const cexpr_t *e, const tatu_value_t *row) {
    const node_t *n = e->node;
    tatu_value_t *args = ar_alloc(TMP, (size_t)n->nitems * sizeof(tatu_value_t));
    for (int i = 0; i < n->nitems; i++) args[i] = eval(e->items[i], row);
    if (!strcmp(n->name, "TYPEOF")) return vtext(type_of(args[0]));
    if (!strcmp(n->name, "COALESCE")) {
        for (int i = 0; i < n->nitems; i++) if (args[i].type != TV_NULL) return args[i];
        return vnull();
    }
    for (int i = 0; i < n->nitems; i++) if (args[i].type == TV_NULL) return vnull();
    if (!strcmp(n->name, "ABS")) {
        numbers("ABS", args[0]);
        if (args[0].type == TV_REAL) return checked_real(args[0].r < 0 ? -args[0].r : args[0].r);
        if (args[0].i == INT64_MIN) tatu_fail(TE_EXECUTION, "ABS() excedeu o intervalo INTEGER de 64 bits");
        return vint(args[0].i < 0 ? -args[0].i : args[0].i);
    }
    if (!strcmp(n->name, "ROUND")) {
        numbers("ROUND", args[0]);
        tatu_value_t d = n->nitems > 1 ? args[1] : vint(0);
        numbers("ROUND", d);
        double x = as_double(args[0]), digits = as_double(d);
        if (digits > 308) return vreal(x);
        if (digits < -308) return vreal(x < 0 ? -0.0 : 0.0);
        int places = (int)digits;
        double scale = 1;
        for (int i = 0; i < (places < 0 ? -places : places); i++) scale *= 10;
        return checked_real(round_decimal(x, places, scale));
    }
    return text_function(n->name, args[0]);
}

tatu_value_t eval(const cexpr_t *e, const tatu_value_t *row) {
    if (e->position >= 0) return row[e->position];
    const node_t *n = e->node;
    switch (n->kind) {
    case N_LITERAL: return n->value;
    case N_UNARY: {
        tatu_value_t v = eval(e->a, row);
        if (n->op == OP_NOT) return negate_truth(v);
        if (v.type == TV_NULL) return v;
        numbers("-", v);
        if (v.type == TV_REAL) return checked_real(-v.r);
        if (v.i == INT64_MIN) tatu_fail(TE_EXECUTION, "Negação excedeu o intervalo INTEGER de 64 bits");
        return vint(-v.i);
    }
    case N_BINARY: return binary(n->op, eval(e->a, row), eval(e->b, row));
    case N_ISNULL: return vbool((eval(e->a, row).type == TV_NULL) != n->negated);
    case N_INLIST: {
        tatu_value_t v = eval(e->a, row);
        if (v.type == TV_NULL) return vnull();
        bool null = false, found = false;
        for (int i = 0; i < n->nitems; i++) {
            tatu_value_t eq = binary(OP_EQ, v, eval(e->items[i], row));
            if (eq.type == TV_NULL) null = true;
            else if (eq.b) found = true;
        }
        return found ? vbool(!n->negated) : null ? vnull() : vbool(n->negated);
    }
    case N_BETWEEN: {
        tatu_value_t v = eval(e->a, row);
        tatu_value_t result = logic(OP_AND, binary(OP_GE, v, eval(e->b, row)), binary(OP_LE, v, eval(e->c, row)));
        return n->negated ? negate_truth(result) : result;
    }
    case N_LIKE: {
        tatu_value_t v = eval(e->a, row), p = eval(e->b, row);
        if (v.type == TV_NULL || p.type == TV_NULL) return vnull();
        if (v.type != TV_TEXT || p.type != TV_TEXT) tatu_fail(TE_TYPE, "LIKE só funciona com TEXT");
        return vbool(match_like(v.s, p.s) != n->negated);
    }
    case N_CALL: return scalar_eval(e, row);
    default: tatu_fail(TE_EXECUTION, "Expressão não suportada: %s", node_text(n));
    }
}

tatu_value_t evaluate_constant(const node_t *n) {
    scope_t empty = { 0 };
    return eval(compile_expr(n, &empty), NULL);
}

tatu_value_t aggregate(const node_t *call, tatu_value_t *values, int count) {
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (values[i].type == TV_NULL) continue;
        bool duplicate = false;
        if (call->distinct) for (int j = 0; j < n; j++) if (value_eq(values[j], values[i])) { duplicate = true; break; }
        if (!duplicate) values[n++] = values[i];
    }
    if (!strcmp(call->name, "COUNT")) return vint(n);
    if (!n) return vnull();
    if (!strcmp(call->name, "MIN") || !strcmp(call->name, "MAX")) {
        tatu_value_t best = values[0];
        for (int i = 1; i < n; i++) {
            if (family(best) != family(values[i])) tatu_fail(TE_TYPE, "%s() encontrou tipos misturados", call->name);
            int cmp = value_cmp(values[i], best);
            if ((!strcmp(call->name, "MIN") && cmp < 0) || (!strcmp(call->name, "MAX") && cmp > 0)) best = values[i];
        }
        return best;
    }
    tatu_value_t total = vint(0);
    for (int i = 0; i < n; i++) { numbers(call->name, values[i]); total = binary(OP_ADD, total, values[i]); }
    return !strcmp(call->name, "AVG") ? checked_real(as_double(total) / n) : total;
}

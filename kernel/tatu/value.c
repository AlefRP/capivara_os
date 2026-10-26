/*
 * Tipos de dados e valores (o datatypes.py do Tatu em Python).
 *
 * O Tatu DB conhece quatro tipos — o suficiente para aprender:
 *
 *     INTEGER   números inteiros       42
 *     REAL      números com vírgula    3.14
 *     TEXT      textos                 'tatu-bola'
 *     BOOLEAN   verdadeiro / falso     TRUE
 *
 * NULL representa "valor ausente" e cabe em qualquer tipo, a menos que a
 * coluna seja NOT NULL.
 *
 * Sem biblioteca C, também moram aqui a conversão de número real para texto
 * (no mesmo formato do repr() do Python, para o arquivo .tatu sair igual) e
 * a leitura de um número real escrito em texto.
 */
#include "tatu/internal.h"

static const struct { const char *alias, *type; } aliases[] = {
    { "INTEGER", "INTEGER" }, { "INT", "INTEGER" }, { "BIGINT", "INTEGER" }, { "SMALLINT", "INTEGER" },
    { "REAL", "REAL" }, { "FLOAT", "REAL" }, { "DOUBLE", "REAL" }, { "NUMERIC", "REAL" }, { "DECIMAL", "REAL" },
    { "TEXT", "TEXT" }, { "VARCHAR", "TEXT" }, { "CHAR", "TEXT" }, { "STRING", "TEXT" },
    { "BOOLEAN", "BOOLEAN" }, { "BOOL", "BOOLEAN" },
};

const char *normalize_type(const char *name) {
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++)
        if (!strcasecmp(name, aliases[i].alias)) return aliases[i].type;
    return NULL;
}

const char *type_of(tatu_value_t v) {
    switch (v.type) {
    case TV_INT: return "INTEGER";
    case TV_REAL: return "REAL";
    case TV_TEXT: return "TEXT";
    case TV_BOOL: return "BOOLEAN";
    default: return "NULL";
    }
}

int family(tatu_value_t v) {
    switch (v.type) {
    case TV_INT: case TV_REAL: return FAM_NUMBER;
    case TV_TEXT: return FAM_TEXT;
    case TV_BOOL: return FAM_BOOL;
    default: return FAM_NULL;
    }
}

int type_family(const char *type) {
    if (!strcmp(type, "TEXT")) return FAM_TEXT;
    if (!strcmp(type, "BOOLEAN")) return FAM_BOOL;
    return FAM_NUMBER;
}

double as_double(tatu_value_t v) { return v.type == TV_INT ? (double)v.i : v.r; }

static bool real_is_int(double r, int64_t *out) {
    if (!(r >= -9223372036854775808.0 && r < 9223372036854775808.0)) return false;
    int64_t i = (int64_t)r;
    if ((double)i != r) return false;
    if (out) *out = i;
    return true;
}

/* O == do Python: números comparam pelo valor (1 == 1.0), o resto pelo tipo. */
bool value_eq(tatu_value_t a, tatu_value_t b) {
    int fa = family(a), fb = family(b);
    if (fa != fb) return false;
    switch (fa) {
    case FAM_NULL: return true;
    case FAM_TEXT: return !strcmp(a.s, b.s);
    case FAM_BOOL: return a.b == b.b;
    default:
        if (a.type == TV_INT && b.type == TV_INT) return a.i == b.i;
        if (a.type != b.type) {
            int64_t integer;
            tatu_value_t iv = a.type == TV_INT ? a : b, rv = a.type == TV_REAL ? a : b;
            return real_is_int(rv.r, &integer) && integer == iv.i;
        }
        return as_double(a) == as_double(b);
    }
}

/* Valores iguais (para value_eq) precisam ter o mesmo hash: um REAL inteiro
 * usa o hash do INTEGER correspondente. */
uint32_t value_hash(tatu_value_t v) {
    uint64_t h;
    int64_t i;
    switch (v.type) {
    case TV_TEXT: {
        uint32_t x = 2166136261u;                         /* FNV-1a */
        for (const unsigned char *p = (const unsigned char *)v.s; *p; p++) x = (x ^ *p) * 16777619u;
        return x;
    }
    case TV_BOOL: return v.b ? 0x9E3779B9u : 0x7F4A7C15u;
    case TV_INT: h = (uint64_t)v.i; break;
    case TV_REAL:
        if (real_is_int(v.r, &i)) h = (uint64_t)i;
        else memcpy(&h, &v.r, 8);
        break;
    default: return 0;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return (uint32_t)h;
}

/* Ordem dentro da mesma família. Textos comparam por código Unicode, que é a
 * mesma ordem dos bytes em UTF-8. FALSE vem antes de TRUE. */
static int int_real_cmp(int64_t i, double r) {
    /* Converter i para double antes de comparar perderia os bits além de
     * 2^53: 9007199254740993 não é igual a 9007199254740992.0. */
    if (r >= 9223372036854775808.0) return -1;
    if (r < -9223372036854775808.0) return 1;
    int64_t truncated = (int64_t)r;
    if (i < truncated) return -1;
    if (i > truncated) return 1;
    return (double)i < r ? -1 : (double)i > r;
}

int value_cmp(tatu_value_t a, tatu_value_t b) {
    switch (family(a)) {
    case FAM_TEXT: {
        int c = strcmp(a.s, b.s);
        return c < 0 ? -1 : c > 0;
    }
    case FAM_BOOL: return (int)a.b - (int)b.b;
    case FAM_NUMBER:
        if (a.type == TV_INT && b.type == TV_INT) return a.i < b.i ? -1 : a.i > b.i;
        if (a.type == TV_INT) return int_real_cmp(a.i, b.r);
        if (b.type == TV_INT) return -int_real_cmp(b.i, a.r);
        return as_double(a) < as_double(b) ? -1 : as_double(a) > as_double(b);
    default: return 0;
    }
}

/* Converte para o tipo da coluna, ou reclama. Só conversões sem perda:
 * INTEGER → REAL (3 → 3.0) e REAL inteiro → INTEGER (3.0 → 3). */
tatu_value_t coerce(tatu_value_t v, const char *type, const char *column) {
    if (v.type == TV_NULL) return v;
    const char *actual = type_of(v);
    if (!strcmp(actual, type)) return v;
    if (!strcmp(type, "REAL") && v.type == TV_INT) return vreal((double)v.i);
    int64_t i;
    if (!strcmp(type, "INTEGER") && v.type == TV_REAL && real_is_int(v.r, &i)) return vint(i);
    tatu_fail(TE_TYPE, "A coluna '%s' é %s, mas recebeu %s (%s)", column, type, actual, sql_literal(v));
}

/* ------------------------------------------------------------------ UTF-8 */
int utf8_len_n(const char *s, int bytes) {
    int n = 0;
    for (int i = 0; i < bytes && s[i]; i++) n += ((unsigned char)s[i] & 0xC0) != 0x80;
    return n;
}
int utf8_len(const char *s) { return utf8_len_n(s, 0x7FFFFFFF); }

/* ------------------------------------------------------------------ números reais */
static double pow10(int e) {
    static const double exact[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22 };
    if (e >= 0 && e <= 22) return exact[e];
    double r = 1;
    if (e > 0) { while (e > 22) { r *= 1e22; e -= 22; } return r * exact[e]; }
    while (e < -22) { r /= 1e22; e += 22; }
    return r / exact[-e];
}

/* mantissa × 10^exp. Com mantissa < 2^53 e |exp| ≤ 22 a conta tem um só
 * arredondamento e o resultado é o double mais próximo (o "caminho rápido"
 * clássico); fora disso, pode errar no último bit. */
static double from_digits(uint64_t m, int e) {
    if (!m) return 0;                        /* inclusive 0e9999 */
    if (e > 308) return (double)m * pow10(e);
    if (e < -350) return 0;
    if (e >= 0) return (double)m * pow10(e);
    if (e >= -22) return (double)m / pow10(-e);
    double r = (double)m;
    while (e < -22) { r /= 1e22; e += 22; }
    return r / pow10(-e);
}

/* Os p primeiros algarismos de x (> 0), arredondados, e o expoente decimal
 * do primeiro: x ≈ 0.d1d2..dp × 10^(e+1). */
static uint64_t to_digits(double x, int p, int *e10) {
    int e = 0;
    double y = x;
    while (y >= 10) { y /= 10; e++; }
    while (y < 1) { y *= 10; e--; }
    int shift = e - p + 1;
    double scaled = shift >= 0 ? x / pow10(shift) : shift >= -308 ? x * pow10(-shift) : y * pow10(p - 1);
    uint64_t m = (uint64_t)scaled;
    double frac = scaled - (double)m;
    if (frac > 0.5 || (frac == 0.5 && (m & 1))) m++;
    uint64_t limit = 1;
    for (int i = 0; i < p; i++) limit *= 10;
    if (m >= limit) { m /= 10; e++; }
    *e10 = e;
    return m;
}

/* Como o repr() do Python: os menos algarismos que, lidos de volta, dão o
 * mesmo número. Notação científica fora de 1e-4 ≤ |x| < 1e16. */
void real_repr(double x, char *out) {
    if (x != x) { strcpy(out, "nan"); return; }
    bool neg = x < 0 || (x == 0 && 1 / x < 0);
    if (neg) { *out++ = '-'; x = -x; }
    if (x == 0) { strcpy(out, "0.0"); return; }
    if (x > 1.7976931348623157e308) { strcpy(out, "inf"); return; }
    uint64_t m = 0;
    int e = 0, p;
    for (p = 1; p <= 17; p++) {
        m = to_digits(x, p, &e);
        if (from_digits(m, e - p + 1) == x) break;
    }
    if (p > 17) p = 17;
    char d[24];
    int nd = 0;
    for (uint64_t t = m; nd < p; t /= 10) d[p - 1 - nd++] = (char)('0' + t % 10);
    while (nd > 1 && d[nd - 1] == '0') nd--;              /* tira zeros do fim */
    char *o = out;
    if (e >= -4 && e < 16) {
        if (e < 0) {
            *o++ = '0'; *o++ = '.';
            for (int i = -1; i > e; i--) *o++ = '0';
            for (int i = 0; i < nd; i++) *o++ = d[i];
        } else {
            for (int i = 0; i <= e; i++) *o++ = i < nd ? d[i] : '0';
            *o++ = '.';
            if (nd > e + 1) for (int i = e + 1; i < nd; i++) *o++ = d[i];
            else *o++ = '0';
        }
    } else {
        *o++ = d[0];
        if (nd > 1) { *o++ = '.'; for (int i = 1; i < nd; i++) *o++ = d[i]; }
        *o++ = 'e';
        *o++ = e < 0 ? '-' : '+';
        int ae = e < 0 ? -e : e;
        if (ae >= 100) *o++ = (char)('0' + ae / 100);
        *o++ = (char)('0' + ae / 10 % 10);
        *o++ = (char)('0' + ae % 10);
    }
    *o = 0;
}

/* O formato "%.10g" do Python (usado para mostrar REAL não inteiro). */
static void real_g10(double x, char *out) {
    if (x != x || x > 1.7976931348623157e308 || x < -1.7976931348623157e308) { real_repr(x, out); return; }
    if (x < 0) { *out++ = '-'; x = -x; }
    if (x == 0) { strcpy(out, "0"); return; }
    int e;
    uint64_t m = to_digits(x, 10, &e);
    char d[12];
    for (int i = 9; i >= 0; i--, m /= 10) d[i] = (char)('0' + m % 10);
    int nd = 10;
    while (nd > 1 && d[nd - 1] == '0') nd--;
    char *o = out;
    if (e >= -4 && e < 10) {
        if (e < 0) {
            *o++ = '0'; *o++ = '.';
            for (int i = -1; i > e; i--) *o++ = '0';
            for (int i = 0; i < nd; i++) *o++ = d[i];
        } else {
            for (int i = 0; i <= e; i++) *o++ = i < nd ? d[i] : '0';
            if (nd > e + 1) { *o++ = '.'; for (int i = e + 1; i < nd; i++) *o++ = d[i]; }
        }
    } else {
        *o++ = d[0];
        if (nd > 1) { *o++ = '.'; for (int i = 1; i < nd; i++) *o++ = d[i]; }
        *o++ = 'e';
        *o++ = e < 0 ? '-' : '+';
        int ae = e < 0 ? -e : e;
        if (ae >= 100) *o++ = (char)('0' + ae / 100);
        *o++ = (char)('0' + ae / 10 % 10);
        *o++ = (char)('0' + ae % 10);
    }
    *o = 0;
}

/* Lê "3.14", ".5", "1e-05", "-2.5E+3". */
bool parse_real(const char *s, size_t n, double *out) {
    size_t i = 0;
    bool neg = false;
    if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
    uint64_t m = 0;
    int e = 0, digits = 0;
    bool any = false;
    for (; i < n && isdigit(s[i]); i++, any = true) {
        if (digits < 19) { m = m * 10 + (uint64_t)(s[i] - '0'); if (m) digits++; }
        else e++;
    }
    if (i < n && s[i] == '.') {
        for (i++; i < n && isdigit(s[i]); i++, any = true)
            if (digits < 19) { m = m * 10 + (uint64_t)(s[i] - '0'); if (m) digits++; e--; }
    }
    if (!any) return false;
    if (i < n && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        bool eneg = false;
        if (i < n && (s[i] == '-' || s[i] == '+')) eneg = s[i++] == '-';
        int x = 0;
        if (i >= n || !isdigit(s[i])) return false;
        for (; i < n && isdigit(s[i]); i++) if (x < 10000) x = x * 10 + (s[i] - '0');
        e += eneg ? -x : x;
    }
    if (i != n) return false;
    double r = from_digits(m, e);
    *out = neg ? -r : r;
    return true;
}

/* ------------------------------------------------------------------ texto */
static void literal_into(sb_t *b, tatu_value_t v) {
    char num[40];
    switch (v.type) {
    case TV_NULL: sb_add(b, "NULL"); break;
    case TV_BOOL: sb_add(b, v.b ? "TRUE" : "FALSE"); break;
    case TV_INT: sb_printf(b, "%ld", v.i); break;
    case TV_REAL: real_repr(v.r, num); sb_add(b, num); break;
    case TV_TEXT:
        sb_addc(b, '\'');
        for (const char *p = v.s; *p; p++) {
            if (*p == '\'') sb_addc(b, '\'');
            sb_addc(b, *p);
        }
        sb_addc(b, '\'');
        break;
    }
}

const char *sql_literal(tatu_value_t v) {
    sb_t b;
    sb_init(&b, TMP);
    literal_into(&b, v);
    return sb_str(&b);
}

void tatu_sql_literal(const tatu_value_t *v, char *buf, size_t size) {
    /* A interface pode chamar sem um comando em andamento. Não alocamos nem
     * dependemos do contexto global de tratamento de erros neste caminho. */
    if (!size) return;
    if (v->type != TV_TEXT) {
        if (v->type == TV_REAL) {
            char num[48];
            real_repr(v->r, num);
            strlcpy(buf, num, size);
        } else tatu_format_cell(v, buf, size);
        return;
    }
    size_t n = 0;
    if (n + 1 < size) buf[n++] = '\'';
    for (const char *p = v->s; *p; p++) {
        if (*p == '\'' && n + 1 < size) buf[n++] = '\'';
        if (n + 1 < size) buf[n++] = *p;
    }
    if (n + 1 < size) buf[n++] = '\'';
    buf[n] = 0;
}

/* Como o shell mostra: REAL inteiro com ".1f" (3.0), o resto com ".10g". */
void tatu_format_cell(const tatu_value_t *v, char *buf, size_t size) {
    char num[48];
    switch (v->type) {
    case TV_NULL: strlcpy(buf, "NULL", size); break;
    case TV_BOOL: strlcpy(buf, v->b ? "TRUE" : "FALSE", size); break;
    case TV_INT: snprintf(buf, size, "%ld", v->i); break;
    case TV_TEXT: strlcpy(buf, v->s, size); break;
    case TV_REAL: {
        int64_t i;
        if (real_is_int(v->r, &i) && i > -1000000000000000LL && i < 1000000000000000LL) {
            snprintf(num, sizeof(num), "%s%ld.0", v->r < 0 && i == 0 ? "-" : "", i);
        } else {
            real_g10(v->r, num);
        }
        strlcpy(buf, num, size);
        break;
    }
    }
}

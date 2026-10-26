/* JSON do arquivo .tatu: sem libc, com limite de recursão e Unicode. */
#include "tatu/internal.h"
#include "lib/utf8.h"

jv_t *jv_new(int type) {
    jv_t *v = ar_alloc(TMP, sizeof(*v));
    v->type = type;
    return v;
}

void jv_push(jv_t *arr, jv_t *item) {
    if (!arr->n || (arr->n >= 4 && !(arr->n & (arr->n - 1)))) {
        int cap = arr->n ? arr->n * 2 : 4;
        jv_t **items = ar_alloc(TMP, (size_t)cap * sizeof(*items));
        if (arr->n) memcpy(items, arr->items, (size_t)arr->n * sizeof(*items));
        arr->items = items;
    }
    arr->items[arr->n++] = item;
}

void jv_set(jv_t *obj, const char *key, jv_t *value) {
    for (int i = 0; i < obj->n; i++) {
        if (!strcmp(obj->keys[i], key)) { obj->items[i] = value; return; }
    }
    if (!obj->n || (obj->n >= 4 && !(obj->n & (obj->n - 1)))) {
        int cap = obj->n ? obj->n * 2 : 4;
        const char **keys = ar_alloc(TMP, (size_t)cap * sizeof(*keys));
        if (obj->n) memcpy(keys, obj->keys, (size_t)obj->n * sizeof(*keys));
        obj->keys = keys;
    }
    obj->keys[obj->n] = key;
    jv_push(obj, value);
}

jv_t *jv_get(const jv_t *obj, const char *key) {
    if (!obj || obj->type != JV_OBJECT) return NULL;
    for (int i = 0; i < obj->n; i++) if (!strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

typedef struct { const char *s; size_t p; int depth; char *err; size_t size; bool bad; } jp_t;

static void bad(jp_t *p, const char *msg) {
    if (!p->bad) snprintf(p->err, p->size, "%s (byte %lu)", msg, (unsigned long)p->p + 1);
    p->bad = true;
}

static void ws(jp_t *p) { while (isspace((unsigned char)p->s[p->p])) p->p++; }

static int hex(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = tolower(ch);
    return ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : -1;
}

static uint32_t codeunit(jp_t *p) {
    uint32_t n = 0;
    for (int i = 0; i < 4; i++) {
        int d = hex((unsigned char)p->s[p->p]);
        if (d < 0) { bad(p, "Escape Unicode inválido"); return 0; }
        p->p++;
        n = n * 16 + (uint32_t)d;
    }
    return n;
}

static const char *string(jp_t *p) {
    if (p->s[p->p++] != '"') { bad(p, "Esperava texto entre aspas"); return ""; }
    sb_t b; sb_init(&b, TMP);
    while (!p->bad && p->s[p->p] && p->s[p->p] != '"') {
        unsigned char ch = (unsigned char)p->s[p->p++];
        if (ch < 32) { bad(p, "Caractere de controle no texto JSON"); break; }
        if (ch != '\\') { sb_addc(&b, (char)ch); continue; }
        ch = (unsigned char)p->s[p->p];
        if (!ch) { bad(p, "Escape incompleto"); break; }
        p->p++;
        switch (ch) {
        case '"': case '\\': case '/': sb_addc(&b, (char)ch); break;
        case 'b': sb_addc(&b, '\b'); break;
        case 'f': sb_addc(&b, '\f'); break;
        case 'n': sb_addc(&b, '\n'); break;
        case 'r': sb_addc(&b, '\r'); break;
        case 't': sb_addc(&b, '\t'); break;
        case 'u': {
            uint32_t cp = codeunit(p);
            if (cp >= 0xd800 && cp <= 0xdbff) {
                if (p->s[p->p] != '\\' || p->s[p->p + 1] != 'u') { bad(p, "Par Unicode incompleto"); break; }
                p->p += 2;
                uint32_t low = codeunit(p);
                if (low < 0xdc00 || low > 0xdfff) { bad(p, "Par Unicode inválido"); break; }
                cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            } else if (cp >= 0xdc00 && cp <= 0xdfff) { bad(p, "Par Unicode inválido"); break; }
            if (!cp) { bad(p, "Texto com byte zero não é suportado"); break; }
            char out[4]; int n = utf8_encode(cp, out);
            sb_addn(&b, out, (size_t)n);
            break;
        }
        default: bad(p, "Escape JSON desconhecido"); break;
        }
    }
    if (!p->bad) {
        if (p->s[p->p] != '"') bad(p, "Texto JSON sem fechamento");
        else p->p++;
    }
    return sb_str(&b);
}

static jv_t *value(jp_t *p) {
    ws(p);
    if (++p->depth > 64) { bad(p, "JSON aninhado demais"); return NULL; }
    char ch = p->s[p->p];
    jv_t *v = NULL;
    if (ch == '{' || ch == '[') {
        bool object = ch == '{';
        char end = object ? '}' : ']';
        p->p++;
        v = jv_new(object ? JV_OBJECT : JV_ARRAY);
        ws(p);
        if (p->s[p->p] != end) for (;;) {
            const char *key = NULL;
            if (object) {
                if (p->s[p->p] != '"') { bad(p, "Esperava uma chave JSON"); break; }
                key = string(p); ws(p);
                if (jv_get(v, key)) { bad(p, "Chave JSON repetida"); break; }
                if (p->s[p->p] != ':') { bad(p, "Faltou ':' depois da chave"); break; }
                p->p++;
            }
            jv_t *item = value(p);
            if (p->bad) break;
            if (object) jv_set(v, key, item); else jv_push(v, item);
            ws(p);
            if (p->s[p->p] == end) break;
            if (p->s[p->p] != ',') { bad(p, "Faltou ',' entre os itens JSON"); break; }
            p->p++; ws(p);
        }
        if (!p->bad) {
            if (p->s[p->p] != end) bad(p, "JSON sem fechamento");
            else p->p++;
        }
    } else if (ch == '"') { v = jv_new(JV_STRING); v->s = string(p); }
    else if (!strncmp(p->s + p->p, "null", 4)) { v = jv_new(JV_NULL); p->p += 4; }
    else if (!strncmp(p->s + p->p, "true", 4)) { v = jv_new(JV_BOOL); v->b = true; p->p += 4; }
    else if (!strncmp(p->s + p->p, "false", 5)) { v = jv_new(JV_BOOL); p->p += 5; }
    else if (ch == '-' || isdigit(ch)) {
        size_t start = p->p;
        bool neg = ch == '-', real = false;
        if (neg) p->p++;
        if (!isdigit(p->s[p->p])) bad(p, "Número JSON inválido");
        if (p->s[p->p] == '0') p->p++;
        else while (isdigit(p->s[p->p])) p->p++;
        if (p->s[p->p] == '.') {
            real = true; p->p++;
            if (!isdigit(p->s[p->p])) bad(p, "Número JSON inválido");
            while (isdigit(p->s[p->p])) p->p++;
        }
        if (p->s[p->p] == 'e' || p->s[p->p] == 'E') {
            real = true; p->p++;
            if (p->s[p->p] == '+' || p->s[p->p] == '-') p->p++;
            if (!isdigit(p->s[p->p])) bad(p, "Expoente JSON inválido");
            while (isdigit(p->s[p->p])) p->p++;
        }
        if (!real) {
            uint64_t n = 0, limit = neg ? (1ULL << 63) : ((1ULL << 63) - 1);
            for (size_t i = start + (neg ? 1 : 0); i < p->p; i++) {
                unsigned d = (unsigned)(p->s[i] - '0');
                if (n > (limit - d) / 10) { bad(p, "Inteiro JSON fora de 64 bits"); break; }
                n = n * 10 + d;
            }
            v = jv_new(JV_INT); v->i = neg ? (int64_t)(0 - n) : (int64_t)n;
        } else {
            v = jv_new(JV_REAL);
            if (!parse_real(p->s + start, p->p - start, &v->r)) bad(p, "Número real JSON inválido");
        }
    } else bad(p, "Valor JSON inválido");
    p->depth--;
    return v;
}

jv_t *json_parse(const char *text, char *errbuf, size_t errsize) {
    jp_t p = { .s = text, .err = errbuf, .size = errsize };
    if (errsize) errbuf[0] = 0;
    jv_t *v = value(&p);
    ws(&p);
    if (text[p.p]) bad(&p, "Há texto depois do JSON");
    return p.bad ? NULL : v;
}

static void quote(sb_t *b, const char *s) {
    static const char digits[] = "0123456789abcdef";
    sb_addc(b, '"');
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '"' || ch == '\\') { sb_addc(b, '\\'); sb_addc(b, (char)ch); }
        else if (ch < 32) {
            sb_add(b, "\\u00"); sb_addc(b, digits[ch >> 4]); sb_addc(b, digits[ch & 15]);
        } else sb_addc(b, (char)ch);
    }
    sb_addc(b, '"');
}

void json_write(sb_t *b, const jv_t *v, int level) {
    char number[48];
    switch (v->type) {
    case JV_NULL: sb_add(b, "null"); break;
    case JV_BOOL: sb_add(b, v->b ? "true" : "false"); break;
    case JV_INT: sb_printf(b, "%ld", v->i); break;
    case JV_REAL: real_repr(v->r, number); sb_add(b, number); break;
    case JV_STRING: quote(b, v->s); break;
    case JV_ARRAY: case JV_OBJECT: {
        bool object = v->type == JV_OBJECT, nested = object;
        for (int i = 0; i < v->n; i++) if (v->items[i]->type >= JV_ARRAY) nested = true;
        sb_addc(b, object ? '{' : '[');
        for (int i = 0; i < v->n; i++) {
            if (i) sb_addc(b, ',');
            if (nested) { sb_addc(b, '\n'); for (int j = 0; j <= level; j++) sb_add(b, "  "); }
            else if (i) sb_addc(b, ' ');
            if (object) { quote(b, v->keys[i]); sb_add(b, ": "); }
            json_write(b, v->items[i], level + 1);
        }
        if (nested && v->n) { sb_addc(b, '\n'); for (int j = 0; j < level; j++) sb_add(b, "  "); }
        sb_addc(b, object ? '}' : ']');
        break;
    }
    }
}

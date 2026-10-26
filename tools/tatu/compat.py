#!/usr/bin/env python3
"""Compara o port C com o projeto Python, importado de uma pasta externa.

Uso no container (sem copiar o projeto original para este repositório):
  PYTHONPATH=/original PYTHONDONTWRITEBYTECODE=1 python3 tools/tatu/compat.py
O executável é gerado por tools/tatu/test-host.sh.
"""
import os
from pathlib import Path
import shutil
import subprocess

from tatu import connect
from tatu.demo import DEMO_SQL, DEMO_QUERIES


ROOT = Path("build/tatu/compat")
HOST = Path("build/tatu/tatu-host").resolve()
ROOT.mkdir(parents=True, exist_ok=True)
FIXTURE = ROOT / "original.tatu"
FIXTURE.unlink(missing_ok=True)
db = connect(str(FIXTURE))
db.executescript(DEMO_SQL)
db.executescript("""
CREATE TABLE vazia(x INTEGER, nome TEXT);
CREATE TABLE numeros(id INTEGER PRIMARY KEY, i INTEGER, r REAL, b BOOLEAN, nome TEXT);
INSERT INTO numeros VALUES
  (1,-5,0.1,TRUE,'áéíóú'), (2,-1,1.25,FALSE,''), (3,0,2.5,TRUE,'Bolinha'),
  (4,1,10.0,NULL,' ação '), (5,2,NULL,FALSE,'Straße'), (6,5,-2.5,TRUE,NULL),
  (7,NULL,0.1,NULL,'áéíóú');
CREATE INDEX ix_numeros_i ON numeros(i);
""")
db.close()


def decode_cell(s):
    if s == "NULL":
        return None
    if s in ("TRUE", "FALSE"):
        return s == "TRUE"
    if s.startswith("'"):
        return s[1:-1].replace("''", "'")
    return float(s) if any(c in s for c in ".eE") else int(s)


def native(sql, outfile=None):
    args = [str(HOST), str(FIXTURE), sql]
    if outfile:
        args.append(str(outfile))
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", UBSAN_OPTIONS="halt_on_error=1")
    p = subprocess.run(args, text=True, capture_output=True, env=env, timeout=15)
    if "Sanitizer" in p.stderr or "runtime error:" in p.stderr:
        raise AssertionError(f"Sanitizador em {sql}\n{p.stderr}")
    if p.returncode:
        return None, p.stderr.strip()
    lines, results = p.stdout.splitlines(), []
    i = 0
    while i < len(lines):
        tag, rows, cols = lines[i].split()
        assert tag == "RESULT", p.stdout
        rows, cols = int(rows), int(cols)
        i += 1
        values = []
        for _ in range(rows):
            row = tuple(decode_cell(v) for v in lines[i].split("\t")) if cols else ()
            assert len(row) == cols
            values.append(row)
            i += 1
        results.append((cols, values))
    return results, ""


def python(sql):
    path = ROOT / "python-work.tatu"
    shutil.copyfile(FIXTURE, path)
    original = connect(str(path))
    try:
        return [(len(r.columns), r.rows) for r in original.executescript(sql)], ""
    except Exception as e:
        return None, str(e)
    finally:
        original.close()


queries = [*DEMO_QUERIES,
    "SELECT * FROM numeros ORDER BY id;",
    "SELECT n.id,t.apelido FROM numeros n CROSS JOIN tatus t ORDER BY n.id,t.id LIMIT 12 OFFSET 3;",
    "SELECT a.id,b.id FROM numeros a INNER JOIN numeros b ON a.i=b.i ORDER BY a.id;",
    "SELECT a.id,b.nome FROM numeros a LEFT JOIN vazia b ON a.i=b.x ORDER BY a.id;",
    "SELECT * FROM vazia;", "SELECT COUNT(*),COUNT(x),SUM(x),AVG(x),MIN(x),MAX(x) FROM vazia;",
    "SELECT x,COUNT(*) FROM vazia GROUP BY x;",
    "SELECT COUNT(*) WHERE FALSE;", "SELECT COUNT(*) WHERE NULL;",
    "SELECT b,COUNT(*),SUM(i),AVG(i),MIN(i),MAX(i) FROM numeros GROUP BY b ORDER BY b;",
    "SELECT i%2 AS resto,COUNT(*) AS n FROM numeros GROUP BY i%2 ORDER BY n DESC,resto;",
    "SELECT SUM(DISTINCT r),COUNT(DISTINCT r),AVG(DISTINCT r) FROM numeros;",
    "SELECT COUNT(*) FROM numeros HAVING COUNT(*)>0;",
    "SELECT b FROM numeros GROUP BY b HAVING b IS NULL;",
    "SELECT DISTINCT r FROM numeros ORDER BY r;",
    "SELECT DISTINCT b,r FROM numeros ORDER BY b,r;",
    "SELECT nome FROM numeros ORDER BY i DESC,id LIMIT 3 OFFSET 1;",
    "SELECT id,UPPER(nome),LOWER(nome),LENGTH(nome),TRIM(nome),TYPEOF(nome) FROM numeros ORDER BY id;",
    "SELECT COALESCE(nome,'sem nome') FROM numeros ORDER BY id;",
    "SELECT id,i/2,i%2,-i,ABS(i),ROUND(r),ROUND(r,1) FROM numeros ORDER BY id;",
    "SELECT 'pé-d''água', 'a'||2, 1||TRUE, NULL||'x';",
    "SELECT id FROM numeros WHERE i=2;", "SELECT id FROM numeros WHERE 2=i AND b=FALSE;",
    "SELECT id FROM numeros WHERE i=2.0;", "SELECT id FROM numeros WHERE i=NULL;",
    "SELECT id FROM numeros WHERE i IN (-5,2,NULL);",
    "SELECT id FROM numeros WHERE i NOT IN (-5,2,NULL);",
    "SELECT id FROM numeros WHERE i BETWEEN -1 AND 2;",
    "SELECT id FROM numeros WHERE i NOT BETWEEN -1 AND 2;",
    "SELECT id FROM numeros WHERE nome LIKE '_é%';",
    "SELECT id FROM numeros WHERE nome NOT LIKE '%a%';",
    "SELECT a.id,b.id FROM numeros a LEFT JOIN numeros b ON a.i=b.i WHERE a.i=2;",
    "SELECT id FROM numeros LIMIT 0;", "SELECT id FROM numeros LIMIT 3 OFFSET 1000;",
    "SELECT i AS numero FROM numeros ORDER BY numero;", "SELECT i,r FROM numeros ORDER BY 2 DESC,1;",
    "SELECT 9223372036854775807, -9223372036854775808;",
    "SELECT 9007199254740993=9007199254740992.0,9007199254740993>9007199254740992.0;",
    "SELECT -9007199254740993=-9007199254740992.0,-9007199254740993<-9007199254740992.0;",
    "SELECT ROUND(2.675,2),ROUND(1.005,2),ROUND(-2.675,2),ROUND(250,-2),ROUND(350,-2);",
    "BEGIN; UPDATE numeros SET i=99 WHERE id=1; ROLLBACK; SELECT i FROM numeros WHERE id=1;",
    "BEGIN; DELETE FROM numeros WHERE id=1; ROLLBACK; SELECT COUNT(*) FROM numeros;",
    "BEGIN; DROP TABLE numeros; ROLLBACK; SELECT COUNT(*) FROM numeros;",
    "BEGIN; CREATE INDEX novo ON numeros(r); ROLLBACK; EXPLAIN SELECT id FROM numeros WHERE r=0.1;",
    "INSERT INTO numeros(id,i) VALUES(10,10); SELECT i,r,b,nome FROM numeros WHERE id=10;",
    "UPDATE numeros SET i=i+1,r=i WHERE id=1; SELECT i,r FROM numeros WHERE id=1;",
    "DELETE FROM numeros WHERE id=1; SELECT COUNT(*) FROM numeros;",
    "CREATE TABLE sozinha(x TEXT); INSERT INTO sozinha VALUES('a'); SELECT * FROM sozinha;",
    "SELECT desconhecida FROM numeros;", "SELECT id FROM numeros a JOIN numeros b ON a.id=b.id;",
    "SELECT 1/0;", "SELECT 'x'+1;", "SELECT 'x'=1;", "SELECT ABS('x');",
    "SELECT outra(1);", "SELECT LENGTH(1);", "SELECT ROUND(1,2,3);",
    "SELECT SUM(COUNT(*)) FROM numeros;", "SELECT SUM(i),nome FROM numeros;",
    "SELECT COUNT(*) FROM numeros WHERE SUM(i)>0;", "SELECT id FROM numeros HAVING id>0;",
    "SELECT i FROM numeros ORDER BY 2;", "SELECT i FROM numeros LIMIT -1;",
    "SELECT ?;", "SELECT tatus.* FROM numeros;",
    "INSERT INTO numeros(id) VALUES(1);", "UPDATE numeros SET i='x';",
    "BEGIN; BEGIN;", "COMMIT;", "ROLLBACK;", "SELECT FROM;",
]

for a in ("NULL", "TRUE", "FALSE", "0", "1", "''", "'x'"):
    queries.append(f"SELECT NOT {a}, {a} IS NULL, {a} IS NOT NULL;")
    for b in ("NULL", "TRUE", "FALSE", "0", "1"):
        queries.append(f"SELECT {a} AND {b}, {a} OR {b};")

queries.append(";".join(
    f"SELECT ROUND({value},{places})" for value in
    ("0.075", "0.145", "0.285", "1.005", "2.675", "10.005", "-10.005", "0.0000001",
     "99999999999999.5", "123456789012345.0", "0.00000000000000000000000000015")
    for places in (-3, -1, 0, 1, 2, 4, 10, 22, 28)
))


def typed(results):
    """BOOLEAN não pode passar por INTEGER só porque False == 0 no Python."""
    if results is None:
        return None
    return [(cols, [[(type(cell).__name__, cell) for cell in row] for row in rows])
            for cols, rows in results]

failed = 0
for sql in queries:
    expected, pyerr = python(sql)
    actual, cerr = native(sql)
    if typed(expected) != typed(actual):
        failed += 1
        print(f"DIFERENÇA: {sql}\n  Python: {expected!r} {pyerr}\n  C: {actual!r} {cerr}")

# O C deve escrever um arquivo que o Python consiga reabrir, com tipos e índices.
outfile = ROOT / "capivara.tatu"
actual, error = native("INSERT INTO numeros(id,nome,r) VALUES(20,'Tatá',0.125); CREATE INDEX ix_numeros_nome ON numeros(nome);", outfile)
assert actual is not None, error
roundtrip = connect(str(outfile))
assert roundtrip.execute("SELECT nome,r FROM numeros WHERE id=20").rows == [("Tatá", 0.125)]
assert roundtrip.execute("SELECT COUNT(*) FROM numeros").scalar() == 8
assert any(i.name == "ix_numeros_nome" for i in roundtrip.storage.get_table("numeros").indexes.values())
roundtrip.close()
print(f"Compatibilidade Tatu: {len(queries)} consultas, {failed} diferença(s); ida e volta JSON OK.")
raise SystemExit(bool(failed))

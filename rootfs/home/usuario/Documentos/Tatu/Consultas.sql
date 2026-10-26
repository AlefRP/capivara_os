-- No Toca: Arquivo > Novo laboratório de exemplo.
-- Depois abra esta consulta pelo menu Arquivo > Abrir consulta SQL.
-- Ctrl+Enter executa a seleção, ou todo o arquivo quando não há seleção.

SELECT t.apelido, e.nome AS especie
FROM tatus t LEFT JOIN especies e ON t.especie_id = e.id
ORDER BY t.apelido;

SELECT bioma, COUNT(*) AS vezes
FROM avistamentos
GROUP BY bioma
ORDER BY vezes DESC, bioma;

EXPLAIN SELECT apelido FROM tatus WHERE especie_id = 2;

# NERD-BD — Grupo A: Materialização

Motor didático em C11 para as etapas 2 e 3 do NERD-BD. Cada operador consome a
relação completa de entrada e materializa uma `Table` independente antes que o
próximo operador comece.

## Consulta demonstrada

```text
SCAN funcionarios -> FILTER -> JOIN <- SCAN departamentos -> GROUP BY -> PROJECT
```

O executável executa a consulta equivalente a:

```sql
SELECT d.nome, AVG(f.salario)
FROM funcionarios f
JOIN departamentos d ON f.departamento_id = d.id
WHERE f.salario > 5000
GROUP BY d.nome;
```

Ela é executada com Nested Loop Join e Hash Join, que devem produzir os mesmos
grupos.

## Comandos

```sh
make
./miniquery
make test
make bench          # grava bench/results.csv
make sanitize       # requer bibliotecas de sanitizer
make valgrind       # requer Valgrind no Linux/WSL
make clean
```

`make test` inclui a regressão da etapa 2, JOINs, GROUP BY e a consulta
completa. O benchmark pequeno versionado é reproduzido por `make bench`.

## Estrutura

- `data/funcionarios.csv`: dataset da etapa 2.
- `data/funcionarios_etapa3.csv` e `data/departamentos.csv`: datasets pequenos
  para JOIN e testes da etapa 3.
- `bench/generate_data.py`: gerador determinístico para cenários maiores.
- `docs/etapa2.md`: arquitetura e memória da etapa 2.
- `docs/etapa3.md`: JOINs, GROUP BY, métricas e reprodução da etapa 3.

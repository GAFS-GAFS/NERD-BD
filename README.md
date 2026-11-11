# NERD-BD — Grupo A: Materialização

Motor de consultas didático em C11, do projeto de extensão N.E.R.D (UFPR), no modelo
de execução **materializado**: cada operador consome a relação de entrada inteira e
produz uma `Table` nova e independente antes de o próximo operador começar.

Autor: Rafael Lima Dias.

## Documentação

| Documento | Conteúdo |
|---|---|
| [docs/etapa2.md](docs/etapa2.md) | Etapa 2: SCAN, FILTER, PROJECT e AGGREGATE; arquitetura, fluxograma, pipeline, memória e Valgrind, dificuldades e otimizações |
| [docs/etapa3.md](docs/etapa3.md) | Etapa 3: duas tabelas, Nested Loop Join, Hash Join (BUILD/PROBE), GROUP BY, instrumentação, testes, experimentos, gráficos e resposta à questão da etapa |
| [docs/relatorio-etapa2/](docs/relatorio-etapa2/) | Relatório da etapa 2 (LaTeX e PDF) |
| [docs/relatorio-etapa3/](docs/relatorio-etapa3/) | Relatório da etapa 3 (LaTeX e PDF) |
| [docs/materialization.md](docs/materialization.md) | Resumo do modelo materializado |
| [results/tabelas.md](results/tabelas.md) | Tabelas comparativas dos experimentos (medianas) |
| [results/](results/) | Medições brutas (`raw/`), medianas (`resumo_*.csv`), ambiente, datasets e logs de validação |
| [docs/img/](docs/img/) | Fluxogramas e gráficos |

## Consulta da etapa 3

```sql
SELECT d.nome, AVG(f.salario)
FROM funcionarios f
JOIN departamentos d ON f.departamento_id = d.id
WHERE f.salario > 5000
GROUP BY d.nome;
```

```text
SCAN funcionarios -> FILTER -> JOIN <- SCAN departamentos -> PROJECT -> GROUP BY/AVG
```

`miniquery` executa a consulta com Nested Loop Join e com Hash Join, imprime as
métricas de cada JOIN e confere que os dois devolvem os mesmos grupos.

## Comandos

```sh
make                 # compila ./miniquery
./miniquery          # consulta no dataset versionado (data/funcionarios_etapa3.csv, data/departamentos.csv)
./miniquery <funcionarios.csv> <departamentos.csv> [limite_salario]
make test            # testes das etapas 2 e 3
make sanitize        # testes com AddressSanitizer e UndefinedBehaviorSanitizer
make valgrind        # testes no Valgrind Memcheck
make bench           # benchmark rápido no dataset versionado (bench/results.csv)
make experiments     # gera os datasets e roda os experimentos da etapa 3 (results/raw/)
python3 bench/summarize.py   # medianas, tabelas e gráficos (requer matplotlib)
make clean
```

## Estrutura

```text
include/            table.h (dados), materialized.h (operadores e métricas)
src/                table.c, scan.c, materialized.c (FILTER, PROJECT), aggregate.c,
                    join.c (Nested Loop e Hash Join), group_by.c, main.c
tests/              test_materialized.c
bench/              benchmark.c, generate_data.py, run_experiments.py, summarize.py
data/               funcionarios.csv (etapa 2), funcionarios_etapa3.csv e departamentos.csv (etapa 3)
data/experimentos/  datasets pequenos dos experimentos (os grandes são gerados; ver docs/etapa3.md)
docs/               documentação, fluxogramas (.mmd) e img/
results/            resultados dos experimentos e da validação
```

Os datasets dos experimentos são gerados de forma determinística por
`bench/generate_data.py` (semente 2026). Tamanhos e SHA-256 estão em
`results/datasets.csv`.

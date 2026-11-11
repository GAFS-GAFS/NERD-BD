# Modelo de execução materializada

A implementação do Grupo A executa um operador até o fim e armazena toda a sua
saída antes de iniciar o próximo. SCAN, FILTER, PROJECT, JOIN e GROUP BY não retêm
ponteiros para a relação anterior: cada resultado é uma tabela própria, liberada
pelo chamador quando o operador seguinte termina.

- Etapa 2 (SCAN, FILTER, PROJECT, AGGREGATE, memória e Valgrind): [etapa2.md](etapa2.md)
- Etapa 3 (duas tabelas, JOIN, GROUP BY, experimentos): [etapa3.md](etapa3.md)

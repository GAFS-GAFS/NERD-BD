# Modelo de execução materializada

A implementação do Grupo A executa um operador até o fim e armazena toda a sua
saída antes de iniciar o próximo. Portanto, SCAN, FILTER e PROJECT não retêm
ponteiros para a relação anterior: cada resultado é uma tabela própria.

Para a arquitetura, ciclo de vida da memória, testes e limitações, consulte
[etapa2.md](etapa2.md).

import random

def gerar_csv(nome_arquivo, quantidade_linhas):
    departamentos = ['TI', 'RH', 'Vendas', 'Engenharia', 'Marketing', 'Diretoria']
    
    with open(nome_arquivo, 'w') as f:
        f.write('id,departamento,salario,idade\n') # Cabeçalho
        
        for i in range(1, quantidade_linhas + 1):
            depto = random.choice(departamentos)
            # Gera salários entre 3000 e 15000
            salario = round(random.uniform(3000.0, 15000.0), 2)
            # Gera idades entre 20 e 65
            idade = random.randint(20, 65)
            
            f.write(f"{i},{depto},{salario},{idade}\n")
            
    print(f"Sucesso: {nome_arquivo} criado com {quantidade_linhas} linhas.")

# Gera os três volumes exigidos no documento
gerar_csv('data/funcionarios_10k.csv', 10000)
gerar_csv('data/funcionarios_100k.csv', 100000)
gerar_csv('data/funcionarios_1m.csv', 1000000)